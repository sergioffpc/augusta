#!/bin/bash
# Builds the hermetic cooking environment for the Asset Pipeline (ADR-0030)
# entirely under <assets-root>, in Git Bash on Windows: the cooker's _textconv
# module is DirectXTex's WIC loader and COM, which are Windows' alone.
#
# Cooking (always): a self-contained Python environment (uv-managed - no
# system/global Python involved) with the tools/pack Python project installed
# into it (pulling in usd-optimize/usd-validation-nvidia/pynacl/blake3 as its
# own dependencies, plus the two small native _meshoptimizer/_textconv
# extension modules built and placed into that same project - ADR-0030's
# cooker is pure Python otherwise, including the pack format and key
# generation), a signing keypair, and the authoring/packs/keys content dirs
# (the cooker reads stages from authoring/, relative to it). Everything the
# installed `augusta-pack` command needs to run lives under this one root, so
# it never depends on what's on PATH in whatever shell it's invoked from.
# Composer and the Adobe USD plugins have their own opt-in bootstrap at
# tools/composer/scripts/bootstrap.sh.
#
# Usage: tools/pack/scripts/bootstrap.sh <assets-root>
#   The root of the hermetic environment this script builds. Everything below
#   (content dirs, the uv-managed Python venv) is created under it - nothing
#   here belongs in git (least of all <assets-root>/keys).
set -euo pipefail

# The worked authoring/ tree seeded into a fresh environment, relative to
# tools/composer/examples/authoring.
readonly EXAMPLE_PIECES=(
  maps/firebase.usda
  characters/soldier.usda
  sounds/gunshot.wav
  sounds/hit_marker.wav
  sounds/hit_taken.wav
  sounds/death.wav
  sounds/match_won.wav
  sounds/match_lost.wav
  scripts/parameters/rules_of_engagement.lua
  scripts/rules/last_man_standing.lua
  scenarios/firebase.yaml
)

# Whether both native modules are in the pack package. pybind11 tags the
# actual filename with the Python ABI (e.g. _meshoptimizer.cp312-win_amd64.pyd)
# - Python's import machinery resolves that back to the plain module name
# regardless, so check by glob.
# Arguments:
#   The pack package directory.
native_modules_built() {
  compgen -G "$1/_meshoptimizer*.pyd" >/dev/null \
    && compgen -G "$1/_textconv*.pyd" >/dev/null
}

main() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) ;;
    *)
      echo "bootstrap: the cooker runs on Windows only - its _textconv" \
        "module is DirectXTex's WIC loader and COM (ADR-0017)." >&2
      exit 1
      ;;
  esac
  if (($# != 1)); then
    echo "usage: $0 <assets-root>" >&2
    exit 2
  fi

  local assets_root pack_project repo_root
  assets_root="$(cygpath -u "$1")"
  pack_project="$(cd "$(dirname "$0")/.." && pwd)"
  repo_root="$(cd "${pack_project}/../.." && pwd)"

  # uv manages its own Python interpreters - no system-wide Python install
  # needed. The VC++ runtime usd-optimize's USD DLLs import comes from
  # scripts/bootstrap.sh too.
  local missing=() command
  for command in git uv cmake ninja; do
    command -v "${command}" >/dev/null 2>&1 || missing+=("${command}")
  done
  if ((${#missing[@]} > 0)); then
    echo "bootstrap: these commands are required but aren't on PATH:" \
      "${missing[*]}. Run scripts/bootstrap.sh first, then re-run this" \
      "script from a new terminal." >&2
    exit 1
  fi

  # --- Content dirs ---
  local authoring_dir="${assets_root}/authoring"
  local packs_dir="${assets_root}/packs"
  local keys_dir="${assets_root}/keys"
  local python_dir="${assets_root}/python"
  local bin_dir="${assets_root}/bin"
  mkdir -p "${authoring_dir}" "${packs_dir}" "${keys_dir}" "${bin_dir}"

  # A small worked authoring/ tree (tools/composer/examples/authoring -
  # committed, unlike everything else under the assets root) so a fresh
  # environment has something to cook straight away (`augusta-pack
  # firebase`): one map (ADR-0015), one character (ADR-0040), placeholder cue
  # sounds (ADR-0020), a Parameters script (ADR-0039) and rules (ADR-0022),
  # and the scenario's manifest (ADR-0041) composing them. Seeded piece by
  # piece rather than as one tree, so each survives local edits
  # independently - left alone once it exists, like the signing key below.
  local example_root="${repo_root}/tools/composer/examples/authoring" piece
  for piece in "${EXAMPLE_PIECES[@]}"; do
    if [[ -e "${authoring_dir}/${piece}" ]]; then
      echo "Example ${piece} already exists at ${authoring_dir}/${piece} -" \
        "leaving it as is."
    else
      echo "Seeding example ${piece} at ${authoring_dir}/${piece}..."
      mkdir -p "$(dirname "${authoring_dir}/${piece}")"
      cp "${example_root}/${piece}" "${authoring_dir}/${piece}"
    fi
  done

  # --- Hermetic Python (ADR-0015/ADR-0016): the pack project ---
  # Installed with `uv tool install`, the mechanism uv provides for exactly
  # this: an isolated, uv-managed venv per tool (no system Python involved)
  # plus the tool's own console scripts placed in a bin directory. Both are
  # redirected under the assets root - the venv to python/pack, the commands
  # (augusta-pack, augusta-keygen, augusta-inspect, augusta-verify,
  # augusta-publish) to bin. tools/pack (this repo's own Python project - see
  # its pyproject.toml) is installed editable, pulling in usd-optimize
  # (Python API only, no CLI) and usd-validation-nvidia (CLI) as its
  # dependencies, so local edits to it take effect without rerunning this
  # script.
  if [[ -f "${python_dir}/pyvenv.cfg" ]]; then
    echo "bootstrap: ${python_dir} is a plain venv from an earlier version of" \
      "this script; delete it and re-run (it is fully regenerated)." >&2
    exit 1
  fi
  echo "Installing pack (from ${pack_project}) into ${python_dir}, commands" \
    "into ${bin_dir} (uv tool)..."
  UV_TOOL_DIR="$(cygpath -w "${python_dir}")" \
  UV_TOOL_BIN_DIR="$(cygpath -w "${bin_dir}")" \
    uv tool install --python 3.12 --force --editable \
    "$(cygpath -w "${pack_project}")"

  # --- Native modules build (ADR-0030) ---
  # tools/pack/cpp is its own standalone CMake project (own
  # vcpkg.json/CMakePresets.json, independent of the client/server build)
  # that builds the two pybind11 modules straight into ../src/pack, so
  # `import pack._meshoptimizer`/`_textconv` just work with no separate copy
  # step. PYTHON_EXECUTABLE (the variable pybind11's vcpkg port's legacy
  # FindPythonInterp reads) points cmake at this venv, so the built
  # extensions' ABI matches the interpreter that imports them. Through
  # scripts/vcenv.cmd, as the Makefile builds: Ninja finds cl.exe only in the
  # Visual Studio Build Tools environment.
  local native_source_dir="${pack_project}/cpp"
  local pack_package_dir="${pack_project}/src/pack"
  if ! native_modules_built "${pack_package_dir}"; then
    echo "Building the native modules (${native_source_dir})..."
    local vcenv="${repo_root}/scripts/vcenv.cmd"
    "${vcenv}" cmake --preset windows -S "$(cygpath -w "${native_source_dir}")" \
      "-DPYTHON_EXECUTABLE=$(cygpath -w "${python_dir}/pack/Scripts/python.exe")"
    "${vcenv}" cmake --build "$(cygpath -w "${native_source_dir}/build/x64-windows")"
    if ! native_modules_built "${pack_package_dir}"; then
      echo "bootstrap: the native modules build did not produce both" \
        "_meshoptimizer and _textconv in ${pack_package_dir} - see the" \
        "cmake output above." >&2
      exit 1
    fi
  fi

  # Not regenerated on a re-run: overwriting it would silently invalidate
  # every pack already signed with the old key and the public key already
  # deployed for verification (main.cpp's <public_key_path> argument,
  # ADR-0018). Goes through the augusta-keygen entry point (pack/keys.py,
  # pynacl - pure Python, no native module or CLI binary involved).
  local signing_key_prefix="${keys_dir}/signing"
  if [[ -f "${signing_key_prefix}.key" ]]; then
    echo "Signing keypair already exists at ${signing_key_prefix}.key/.pub -" \
      "leaving it as is."
  else
    echo "Generating Ed25519 signing keypair at ${signing_key_prefix}.key/.pub..."
    "${bin_dir}/augusta-keygen.exe" "$(cygpath -w "${signing_key_prefix}")"
  fi

  echo
  echo "Hermetic environment ready at ${assets_root} (never commit any of it," \
    "especially ${keys_dir}):"
  echo "  - ${authoring_dir}: scenario folders (a stage and its Lua scripts" \
    "each) - a convenient place to keep them, not a boundary the cooker" \
    "enforces"
  echo "  - ${packs_dir}: signed packs cooked via the cooker"
  echo "  - ${keys_dir}: Ed25519 signing keypair (signing.key/signing.pub)"
  echo "  - ${bin_dir}: augusta-pack, augusta-keygen, augusta-inspect," \
    "augusta-verify, augusta-publish"
  echo "  - ${python_dir}: hermetic Python venv (uv tool), pack installed" \
    "editable from tools/pack (includes the native _meshoptimizer/_textconv" \
    "modules - ${pack_package_dir})"
  echo
  echo "Cook the example scenario: ${bin_dir}/augusta-pack.exe firebase"
}

main "$@"

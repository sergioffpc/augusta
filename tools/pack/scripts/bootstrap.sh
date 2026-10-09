#!/bin/bash
# Builds the hermetic cooking environment for the Asset Pipeline (ADR-0030)
# entirely under <assets-root>, in Git Bash on Windows or on Linux - the
# platforms vcpkg's DirectXTex port, which the cooker's _textconv module wraps
# (ADR-0017), builds for.
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

# Whether this runs in Git Bash on Windows, rather than on Linux.
on_windows() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# Prints a path as this platform's native programs (uv, cmake) take it: in
# Windows form under Git Bash, as is on Linux.
# Arguments:
#   The path.
native_path() {
  if on_windows; then
    cygpath -w "$1"
  else
    echo "$1"
  fi
}

# Whether both native modules are in the pack package. pybind11 tags the
# actual filename with the Python ABI (e.g. _meshoptimizer.cp312-win_amd64.pyd,
# _textconv.cpython-312-x86_64-linux-gnu.so) - Python's import machinery
# resolves that back to the plain module name regardless, so check by glob.
# Arguments:
#   The pack package directory, the extension modules' suffix (pyd or so).
native_modules_built() {
  compgen -G "$1/_meshoptimizer*.$2" >/dev/null \
    && compgen -G "$1/_textconv*.$2" >/dev/null
}

main() {
  if (($# != 1)); then
    echo "usage: $0 <assets-root>" >&2
    exit 2
  fi
  local pack_project repo_root
  pack_project="$(cd "$(dirname "$0")/.." && pwd)"
  repo_root="$(cd "${pack_project}/../.." && pwd)"

  # What differs between the two platforms: the assets root's form, the CMake
  # preset and the build directory it names, the venv's interpreter, the
  # extension modules' suffix, an executable's, the compiler the preset needs
  # beyond the common commands below, and what cmake runs through -
  # scripts/vcenv.cmd on Windows, where Ninja finds cl.exe only in the Visual
  # Studio Build Tools environment, as the Makefile builds.
  local assets_root preset build_dir venv_python module_suffix exe_suffix
  local -a compiler=() build_env=()
  if on_windows; then
    assets_root="$(cygpath -u "$1")"
    preset=windows
    build_dir=x64-windows
    venv_python=Scripts/python.exe
    module_suffix=pyd
    exe_suffix=.exe
    build_env=("${repo_root}/scripts/vcenv.cmd")
  elif [[ "$(uname -s)" = Linux ]]; then
    assets_root="$1"
    preset=linux
    build_dir=x64-linux
    venv_python=bin/python
    module_suffix=so
    exe_suffix=
    compiler=(clang++)
  else
    echo "bootstrap: the cooker runs on Windows (Git Bash) or Linux only -" \
      "vcpkg's DirectXTex port, which its _textconv module wraps" \
      "(ADR-0017), builds for no other platform." >&2
    exit 1
  fi

  # uv manages its own Python interpreters - no system-wide Python install
  # needed. The VC++ runtime usd-optimize's USD DLLs import on Windows, and
  # the clang the Linux preset compiles with, come from scripts/bootstrap.sh
  # too.
  local missing=() command
  for command in git uv cmake ninja "${compiler[@]}"; do
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
  UV_TOOL_DIR="$(native_path "${python_dir}")" \
  UV_TOOL_BIN_DIR="$(native_path "${bin_dir}")" \
    uv tool install --python 3.12 --force --editable \
    "$(native_path "${pack_project}")"

  # --- Native modules build (ADR-0030) ---
  # tools/pack/cpp is its own standalone CMake project (own
  # vcpkg.json/CMakePresets.json, independent of the client/server build)
  # that builds the two pybind11 modules straight into ../src/pack, so
  # `import pack._meshoptimizer`/`_textconv` just work with no separate copy
  # step. PYTHON_EXECUTABLE (the variable pybind11's vcpkg port's legacy
  # FindPythonInterp reads) points cmake at this venv, so the built
  # extensions' ABI matches the interpreter that imports them.
  local native_source_dir="${pack_project}/cpp"
  local pack_package_dir="${pack_project}/src/pack"
  if ! native_modules_built "${pack_package_dir}" "${module_suffix}"; then
    echo "Building the native modules (${native_source_dir})..."
    "${build_env[@]}" cmake --preset "${preset}" \
      -S "$(native_path "${native_source_dir}")" \
      "-DPYTHON_EXECUTABLE=$(native_path "${python_dir}/pack/${venv_python}")"
    "${build_env[@]}" cmake --build \
      "$(native_path "${native_source_dir}/build/${build_dir}")"
    if ! native_modules_built "${pack_package_dir}" "${module_suffix}"; then
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
    "${bin_dir}/augusta-keygen${exe_suffix}" \
      "$(native_path "${signing_key_prefix}")"
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
  echo "Cook the example scenario: ${bin_dir}/augusta-pack${exe_suffix}" \
    "firebase"
}

main "$@"

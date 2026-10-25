#!/bin/bash
# Makes and checks the checkout's toolchain (ADR-0077): every tool at the one
# version scripts/bootstrap/versions.env pins, run from the checkout's
# .toolchain/bin by the editor, the terminal, the hooks and CI.
#
# Usage: scripts/bootstrap/toolchain.sh install|check
#
# install puts each pinned version that is missing into the shared store,
# ${XDG_CACHE_HOME:-~/.cache}/augusta/toolchain/<tool>/<version>/, once per
# machine, then links .toolchain/bin into it (hardlinks or copies on Windows,
# where a symlink may be refused) and writes .toolchain/activate. It is what
# `make toolchain` runs, and quick when nothing changed.
#
# check reads and changes nothing: it fails, naming each culprit, when a tool
# .toolchain/bin links is not the pinned version or does not run, or when a
# path .vscode/settings.json names under .toolchain/ does not exist. It is what
# `make toolchain-check` runs.
#
# Beside the bootstrap scripts, not in the Makefile, as the dev container's
# image build copies only scripts/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly ROOT
readonly VERSIONS="${SCRIPT_DIR}/versions.env"
readonly TOOLCHAIN="${ROOT}/.toolchain"
readonly STORE="${XDG_CACHE_HOME:-${HOME}/.cache}/augusta/toolchain"

# shellcheck source=/dev/null
. "${VERSIONS}"

# The toolchain's tools, by the names the store files them under.
readonly TOOLS=(ruff shfmt shellcheck actionlint gersemi pymarkdown yamllint
  ctcache)

# Describes a tool, in globals: version_name, the versions.env variable that
# pins it, and version, its value; package, what uv installs; command, the
# command it puts in .toolchain/bin; probe, the arguments that make it print its
# version; expected, the line of that output the pinned version prints, or
# nothing when its output names no version the pin gives. A wheel that packages
# another project's binary (shellcheck-py, actionlint-py) adds a fourth number
# of its own to the binary's version, which the binary does not report.
# Without a command substitution, as each forks, slowly on Windows.
# Arguments:
#   The tool.
describe_tool() {
  probe=(--version)
  case "$1" in
    ruff)
      version_name=RUFF_VERSION command=ruff package="ruff==${RUFF_VERSION}"
      expected="ruff ${RUFF_VERSION}"
      ;;
    shfmt)
      # shfmt-py's version is the wheel's, not the shfmt it packages.
      version_name=SHFMT_VERSION command=shfmt
      package="shfmt-py==${SHFMT_VERSION}" expected=
      ;;
    shellcheck)
      version_name=SHELLCHECK_VERSION command=shellcheck
      package="shellcheck-py==${SHELLCHECK_VERSION}"
      expected="version: ${SHELLCHECK_VERSION%.*}"
      ;;
    actionlint)
      version_name=ACTIONLINT_VERSION command=actionlint
      package="actionlint-py==${ACTIONLINT_VERSION}"
      expected="${ACTIONLINT_VERSION%.*}"
      ;;
    gersemi)
      version_name=GERSEMI_VERSION command=gersemi
      package="gersemi==${GERSEMI_VERSION}"
      expected="gersemi ${GERSEMI_VERSION}"
      ;;
    pymarkdown)
      version_name=PYMARKDOWN_VERSION command=pymarkdown
      package="pymarkdownlnt==${PYMARKDOWN_VERSION}"
      probe=(version) expected="${PYMARKDOWN_VERSION}"
      ;;
    yamllint)
      version_name=YAMLLINT_VERSION command=yamllint
      package="yamllint==${YAMLLINT_VERSION}"
      expected="yamllint ${YAMLLINT_VERSION}"
      ;;
    ctcache)
      # A commit, which clang-tidy-cache does not report: --help shows it runs.
      version_name=CTCACHE_COMMIT command=clang-tidy-cache
      package="ctcache @ git+https://github.com/matus-chochlik/ctcache@${CTCACHE_COMMIT}"
      probe=(--help) expected=
      ;;
    *)
      echo "toolchain: unknown tool $1" >&2
      exit 1
      ;;
  esac
  version="${!version_name}"
}

windows() {
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# Prints a path as the native programs take it: uv on Windows reads no Git
# Bash path.
native_path() {
  if windows; then
    cygpath -m "$1"
  else
    echo "$1"
  fi
}

# Installs the described tool's pinned version into the store, unless it is
# there already. Its tool environment and the Python that runs it both live
# in the store, so a store restored on another machine (CI's cache) runs.
install_tool() {
  local dir="${STORE}/$1/${version}"
  [[ -f "${dir}/.installed" ]] && return
  echo "toolchain: installing $1 ${version}"
  rm -rf "${dir}"
  mkdir -p "${dir}"
  UV_TOOL_DIR="$(native_path "${dir}/tools")" \
  UV_TOOL_BIN_DIR="$(native_path "${dir}/bin")" \
  UV_PYTHON_INSTALL_DIR="$(native_path "${STORE}/python")" \
  UV_PYTHON_PREFERENCE=only-managed \
    uv tool install --quiet "${package}"
  # Last, so an install cut short is redone.
  touch "${dir}/.installed"
}

# Links a file of the store into .toolchain/bin, unless it is linked already.
# A Windows executable that is running (the editor's ruff server) cannot be
# deleted, but it can be renamed out of the way.
# Arguments:
#   The store's file, the link.
link_file() {
  [[ -e "$2" && "$2" -ef "$1" ]] && return
  if [[ -e "$2" || -L "$2" ]]; then
    rm -f "$2" 2>/dev/null || mv -f "$2" "$2.$$.old"
  fi
  if windows; then
    ln -f "$1" "$2" 2>/dev/null || cp -f "$1" "$2"
  else
    ln -sf "$1" "$2"
  fi
}

install() {
  local tool file name linked=()
  mkdir -p "${TOOLCHAIN}/bin"
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    install_tool "${tool}"
    for file in "${STORE}/${tool}/${version}/bin/${command}"{,.exe}; do
      [[ -e "${file}" ]] || continue
      name="$(basename "${file}")"
      link_file "${file}" "${TOOLCHAIN}/bin/${name}"
      linked+=("${name}")
    done
  done
  # What a previous version linked and this one does not, or renamed away
  # while it ran: removed when nothing runs it any more.
  for file in "${TOOLCHAIN}/bin/"*; do
    name="$(basename "${file}")"
    if [[ " ${linked[*]} " != *" ${name} "* ]]; then
      rm -f "${file}" 2>/dev/null || true
    fi
  done
  cat >"${TOOLCHAIN}/activate" <<EOF
# Puts this checkout's toolchain first on PATH (ADR-0077): source it.
case ":\${PATH}:" in
  *":${TOOLCHAIN}/bin:"*) ;;
  *) export PATH="${TOOLCHAIN}/bin:\${PATH}" ;;
esac
EOF
  # The versions .toolchain/bin now links, which check compares with the pins.
  # Last, so an install cut short leaves the old ones.
  cp "${VERSIONS}" "${TOOLCHAIN}/versions.env"
}

# Reads a whole file into the global contents, carriage returns dropped,
# without forking: nothing when there is no file.
# Arguments:
#   The file.
read_file() {
  contents=
  [[ -f "$1" ]] || return 0
  IFS= read -r -d '' contents <"$1" || true
  contents="${contents//$'\r'/}"
}

check() {
  local failures=() tool linked output path probes status
  if [[ ! -d "${TOOLCHAIN}/bin" ]]; then
    echo "toolchain: ${TOOLCHAIN}/bin does not exist." >&2
    return 1
  fi
  # Every tool prints its version at once, as the pre-commit hook waits on
  # them: each into a file of its own, with its exit status beside it.
  probes="$(mktemp -d)"
  # shellcheck disable=SC2064 # the directory now, not at exit
  trap "rm -rf '${probes}'" EXIT
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    {
      status=0
      "${TOOLCHAIN}/bin/${command}" "${probe[@]}" >"${probes}/${tool}" 2>&1 \
        || status=$?
      echo "${status}" >"${probes}/${tool}.status"
    } &
  done
  wait
  # Each a line between newlines, matched whole, without forking: a fork per
  # tool would cost the hook seconds on Windows.
  read_file "${TOOLCHAIN}/versions.env"
  linked=$'\n'"${contents}"$'\n'
  for tool in "${TOOLS[@]}"; do
    describe_tool "${tool}"
    read_file "${probes}/${tool}.status"
    status="${contents%$'\n'}"
    read_file "${probes}/${tool}"
    output=$'\n'"${contents}"$'\n'
    if [[ "${linked}" != *$'\n'"${version_name}=${version}"$'\n'* ]]; then
      failures+=("${tool}: .toolchain/bin links another version than ${version_name}=${version}")
    elif [[ "${status}" != 0 ]]; then
      failures+=("${tool}: .toolchain/bin/${command} does not run")
    elif [[ -n "${expected}" && "${output}" != *$'\n'"${expected}"$'\n'* ]]; then
      failures+=("${tool}: .toolchain/bin/${command} is not ${version}, it prints: $(head -n 1 <<<"${contents}")")
    fi
  done
  # The editor's binaries: an extension whose path names nothing falls back to
  # its bundled copy without a word. A binary passes with or without .exe, as
  # Windows' bin has only the one and the others' only the other.
  while IFS= read -r path; do
    path="${path#\$\{workspaceFolder\}/}"
    path="${path#\"}"
    if [[ ! -e "${ROOT}/${path}" && ! -e "${ROOT}/${path%.exe}" &&
      ! -e "${ROOT}/${path}.exe" ]]; then
      failures+=(".vscode/settings.json names ${path}, which does not exist")
    fi
  done < <(grep -oE '(\$\{workspaceFolder\}/|")\.toolchain/[^"$:;]*' \
    "${ROOT}/.vscode/settings.json" | sort -u)
  if ((${#failures[@]} > 0)); then
    printf 'toolchain: %s\n' "${failures[@]}" >&2
    return 1
  fi
}

main() {
  case "${1:-}" in
    install) install ;;
    check) check ;;
    *)
      echo "usage: $0 install|check" >&2
      exit 2
      ;;
  esac
}

main "$@"

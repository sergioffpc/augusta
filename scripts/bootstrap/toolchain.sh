#!/bin/bash
# shellcheck disable=SC2034 # the parts in toolchain/ read its globals
# Makes and checks the checkout's toolchain (ADR-0077): every tool at the one
# version scripts/bootstrap/versions.env pins, run from the checkout's
# .toolchain/bin by the editor, the terminal, the hooks and CI.
#
# Usage: scripts/bootstrap/toolchain.sh install|check|store|store-dir
#
# store fills the shared store,
# ${XDG_CACHE_HOME:-~/.cache}/augusta/toolchain/<tool>/<version>/, with each
# pinned version it lacks, once per machine and outside any checkout: what the
# bootstrap's platform step runs, and the dev container's image. store-dir
# prints the store's path, which the bootstrap's uninstall removes.
#
# install fills the store, then links the checkout's .toolchain/bin into it
# (hardlinks or copies on Windows, where a symlink may be refused), with the
# system LLVM's clangd, clang-format and clang-tidy of LLVM_VERSION's major,
# installs Prettier, which package-lock.json pins, into
# .toolchain/node_modules, and writes .toolchain/activate. It is what
# `make toolchain` runs, and quick when nothing changed.
#
# check reads and changes nothing: it fails, naming each culprit, when a tool
# .toolchain/bin links or Prettier is not the pinned version (LLVM's: major) or
# does not run, or when a path .vscode/settings.json names under .toolchain/
# does not exist. It is what `make toolchain-check` and the pre-commit hook
# run.
#
# Each operation in a part of its own, in scripts/bootstrap/toolchain/:
# store.sh, link.sh (install) and check.sh, with the tools they share in
# tools.sh, and Windows' MSVC sysroot, which each of them makes, links or
# checks too, in sysroot.sh. Beside the bootstrap scripts, not in the Makefile, as the dev
# container's image build copies only scripts/.
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
# The LLVM major whose tools .toolchain/bin links.
readonly LLVM_MAJOR="${LLVM_VERSION%%.*}"

# The platform: linux, macos or windows (Git Bash).
case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  MINGW* | MSYS* | CYGWIN*) PLATFORM=windows ;;
  *) PLATFORM=linux ;;
esac
readonly PLATFORM

# Prettier, a Node package that package.json and package-lock.json beside this
# script pin, which npm installs into .toolchain/node_modules.
readonly PACKAGE="${SCRIPT_DIR}/package.json"
readonly LOCKFILE="${SCRIPT_DIR}/package-lock.json"
readonly NODE_MODULES="${TOOLCHAIN}/node_modules"
readonly PRETTIER="${NODE_MODULES}/prettier/bin/prettier.cjs"
# The lockfile npm last installed .toolchain/node_modules from, which npm ci
# deletes as it starts.
readonly INSTALLED_LOCKFILE="${NODE_MODULES}/.augusta-package-lock.json"

# Returns:
#   0 on Windows.
windows() {
  [[ "${PLATFORM}" = windows ]]
}

# Prints a path as the native programs take it: uv and npm on Windows read no
# Git Bash path.
# Arguments:
#   The path.
native_path() {
  if windows; then
    cygpath -m "$1"
  else
    echo "$1"
  fi
}

# Git Bash takes a name without .exe for the .exe beside it, in test, ln, cp
# and cygpath alike: only a directory listing tells the two apart.
# Arguments:
#   The file.
# Returns:
#   0 when its directory lists that exact name.
exists_exactly() {
  local file
  for file in "${1%/*}/"*; do
    [[ "${file}" == "$1" ]] && return 0
  done
  return 1
}

# The parts, each sourced whole: without forking.
# shellcheck source=/dev/null
. "${SCRIPT_DIR}/toolchain/tools.sh"
# shellcheck source=/dev/null
. "${SCRIPT_DIR}/toolchain/store.sh"
# shellcheck source=/dev/null
. "${SCRIPT_DIR}/toolchain/link.sh"
# shellcheck source=/dev/null
. "${SCRIPT_DIR}/toolchain/check.sh"
# shellcheck source=/dev/null
. "${SCRIPT_DIR}/toolchain/sysroot.sh"

main() {
  case "${1:-}" in
    install) install ;;
    check) check ;;
    store) store ;;
    store-dir) echo "${STORE}" ;;
    *)
      echo "usage: $0 install|check|store|store-dir" >&2
      exit 2
      ;;
  esac
}

main "$@"

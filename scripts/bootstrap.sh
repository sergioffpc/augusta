#!/bin/bash
# Bootstraps the development environment (docs/ENGINEERING.md, Developer
# Environment): the Linux/server one on Ubuntu 26.04, the Windows client one in
# Git Bash. The one recipe for each: what differs lives in
# scripts/bootstrap/<platform>.sh, what both share here. .devcontainer's
# Dockerfile runs the Linux toolchain step and its post-create.sh the checkout
# step; a host runs both.
#
# Usage: scripts/bootstrap.sh [toolchain|checkout]
#
# Piped from curl outside a checkout (README.md, Bootstrap), it first clones
# the repository into ${AUGUSTA_DIR:-./augusta} and runs that clone's copy.
set -euo pipefail

readonly REPOSITORY=https://github.com/sergioffpc/augusta.git

# CI's format job pins the same versions; bump them together, with the
# platform files' checksums.
# shellcheck disable=SC2034 # used by the sourced scripts/bootstrap/*.sh
readonly YAMLFMT_VERSION=0.21.0 STYLUA_VERSION=2.5.2 TAPLO_VERSION=0.10.0 \
  LUACHECK_VERSION=1.2.0

# Prints the platform: linux or windows (Git Bash).
platform() {
  case "$(uname -s)" in
    Linux) echo linux ;;
    MINGW* | MSYS* | CYGWIN*) echo windows ;;
    *)
      echo "bootstrap: unsupported platform: $(uname -s) - see README.md" \
        "for macOS (the dev container)." >&2
      exit 1
      ;;
  esac
}

# Downloads a URL to a file and checks its SHA-256.
# Arguments:
#   The URL, the expected SHA-256, the file.
download_verified() {
  curl --proto '=https' --tlsv1.2 -fsSLo "$3" "$1"
  printf '%s  %s\n' "$2" "$3" | sha256sum --check --status \
    || {
      echo "bootstrap: SHA-256 mismatch for $1" >&2
      exit 1
    }
}

# Fails with the list of commands missing from PATH.
# Arguments:
#   The commands.
require_commands() {
  local missing=() command
  for command in "$@"; do
    command -v "${command}" >/dev/null 2>&1 || missing+=("${command}")
  done
  if ((${#missing[@]} > 0)); then
    echo "bootstrap: these commands still aren't on PATH: ${missing[*]}." \
      "Open a new terminal and re-run this script." >&2
    exit 1
  fi
}

ready_checkout() {
  cd "$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
  # LFS's filters only: .githooks' pre-push already runs git lfs pre-push.
  git lfs install --skip-repo
  update_submodules
  bootstrap_vcpkg
  git config core.hooksPath .githooks
}

# Clones the repository and runs the clone's bootstrap, then fetches the LFS
# content the clone checked out as pointers (git-lfs wasn't installed yet).
bootstrap_from_pipe() {
  local dir="${AUGUSTA_DIR:-${PWD}/augusta}"
  if ! command -v git >/dev/null 2>&1; then
    # Only Linux can get here: Git Bash comes with git.
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends git
  fi
  if [[ -d "${dir}/.git" ]]; then
    echo "bootstrap: ${dir} already exists - bootstrapping it as it is." >&2
  else
    git clone "${REPOSITORY}" "${dir}"
  fi
  bash "${dir}/scripts/bootstrap.sh" "$@"
  git -C "${dir}" lfs pull
}

main() {
  # Piped, bash reads the script from stdin and there is no file beside it.
  if [[ ! -f "${BASH_SOURCE[0]:-}" ]]; then
    bootstrap_from_pipe "$@"
    return
  fi
  # Assigned first: an unsupported platform's exit stops the script here, not
  # only the command substitution.
  local platform
  platform="$(platform)"
  # shellcheck source=/dev/null
  . "$(dirname "${BASH_SOURCE[0]}")/bootstrap/${platform}.sh"
  case "${1:-}" in
    toolchain) install_toolchain ;;
    checkout) ready_checkout ;;
    "")
      install_toolchain
      ready_checkout
      bootstrap_done
      ;;
    *)
      echo "usage: $0 [toolchain|checkout]" >&2
      exit 2
      ;;
  esac
}

main "$@"

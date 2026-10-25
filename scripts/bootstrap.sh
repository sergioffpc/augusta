#!/bin/bash
# Bootstraps the development environment (docs/ENGINEERING.md, Developer
# Environment): the Linux/server one on Ubuntu 26.04, the Windows client one in
# Git Bash, and the macOS one on Apple silicon, native as Linux's. The one
# recipe for each: what differs lives in scripts/bootstrap/<platform>.sh, what
# they share here. .devcontainer's Dockerfile runs the Linux toolchain step and
# its post-create.sh the checkout step; a host runs both.
#
# Usage: scripts/bootstrap.sh [toolchain|checkout]
#        scripts/bootstrap.sh uninstall [toolchain|checkout]
#
# Piped from curl outside a checkout (README.md, Bootstrap), it first clones
# the repository into ${AUGUSTA_DIR:-./augusta} and runs that clone's copy.
#
# uninstall undoes them: the checkout step's readying (vcpkg's tool and binary
# cache, the hooks, the submodules; never the repository's files), and what
# the toolchain step installed that was not there before, as its manifest
# records it, newest first. A machine bootstrapped before the manifest has
# none: what it installed then stays.
set -euo pipefail

readonly REPOSITORY=https://github.com/sergioffpc/augusta.git

# CI's format job pins the same versions; bump them together, with the
# platform files' checksums.
# shellcheck disable=SC2034 # used by the sourced scripts/bootstrap/*.sh
readonly YAMLFMT_VERSION=0.21.0 STYLUA_VERSION=2.5.2 TAPLO_VERSION=0.10.0 \
  LUACHECK_VERSION=1.2.0
# ctcache, at the commit the toolchain pins (scripts/bootstrap/versions.env).
# Piped from curl there is no file beside this one, nor any need of it: the
# clone's own copy runs.
if [[ -f "$(dirname "${BASH_SOURCE[0]:-}")/bootstrap/versions.env" ]]; then
  # shellcheck source=/dev/null
  . "$(dirname "${BASH_SOURCE[0]}")/bootstrap/versions.env"
fi
# shellcheck disable=SC2034 # used by the sourced scripts/bootstrap/*.sh
readonly CTCACHE_PACKAGE="ctcache @ git+https://github.com/matus-chochlik/ctcache@${CTCACHE_COMMIT:-}"

# Prints the platform: linux, windows (Git Bash) or macos.
platform() {
  case "$(uname -s)" in
    Linux) echo linux ;;
    MINGW* | MSYS* | CYGWIN*) echo windows ;;
    Darwin) echo macos ;;
    *)
      echo "bootstrap: unsupported platform: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

# Checks the files a checksum list on stdin names, quietly: GNU's sha256sum,
# or on macOS, whose own reads no list from stdin, shasum.
sha256_check() {
  if [[ "$(uname -s)" = Darwin ]]; then
    shasum -a 256 --check --status
  else
    sha256sum --check --status
  fi
}

# Downloads a URL to a file and checks its SHA-256.
# Arguments:
#   The URL, the expected SHA-256, the file.
download_verified() {
  curl --proto '=https' --tlsv1.2 -fsSLo "$3" "$1"
  printf '%s  %s\n' "$2" "$3" | sha256_check \
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

# The manifest: one line per thing the bootstrap installed that was not there
# before, its kind and then its fields, tab-separated (a path may hold
# spaces). Each platform's manifest_file and manifest_append say where it is.

# Arguments:
#   The entry's kind and fields.
# Returns:
#   0 when the manifest holds that entry.
manifest_has() {
  local line
  line="$(
    IFS=$'\t'
    printf '%s' "$*"
  )"
  [[ -f "$(manifest_file)" ]] && grep -qxF -- "${line}" "$(manifest_file)"
}

# Records an entry, once.
# Arguments:
#   The entry's kind and fields.
manifest_add() {
  manifest_has "$@" && return
  (
    IFS=$'\t'
    printf '%s\n' "$*"
  ) | manifest_append
}

# Undoes the manifest's entries, newest first, then removes it. The kinds
# every platform shares are undone here, the rest by its undo_entry.
uninstall_toolchain() {
  local file
  file="$(manifest_file)"
  if [[ ! -f "${file}" ]]; then
    echo "bootstrap: ${file} does not exist: this machine's bootstrap" \
      "recorded nothing to uninstall."
    return
  fi
  require_uninstall_rights
  # No mapfile: macOS's bash 3.2 runs this.
  local lines=() line i
  while IFS= read -r line; do
    [[ -n "${line}" ]] && lines+=("${line}")
  done <"${file}"
  for ((i = ${#lines[@]} - 1; i >= 0; i--)); do
    local fields=()
    IFS=$'\t' read -r -a fields <<<"${lines[i]}"
    echo "bootstrap: undoing ${fields[*]}"
    case "${fields[0]}" in
      lfs-filters) git lfs uninstall --skip-repo ;;
      *) undo_entry "${fields[@]}" ;;
    esac
  done
  remove_manifest
  echo "bootstrap: uninstalled what this machine's bootstrap installed."
}

ready_checkout() {
  cd "$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
  # LFS's filters only: .githooks' pre-push already runs git lfs pre-push.
  # Machine-wide (the global config), so the manifest's.
  git config --global --get filter.lfs.process >/dev/null \
    || manifest_add lfs-filters
  git lfs install --skip-repo
  update_submodules
  bootstrap_vcpkg
  git config core.hooksPath .githooks
}

# Undoes ready_checkout but for LFS's filters, the toolchain's: the
# repository's files and any work in them stay.
unready_checkout() {
  cd "$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
  if [[ "$(git config --get core.hooksPath || true)" = .githooks ]]; then
    git config --unset core.hooksPath
  fi
  unbootstrap_vcpkg
  # Without --force: a submodule with changes of its own stays, and says so.
  if ! git submodule deinit third_party/vcpkg third_party/nvtx; then
    echo "bootstrap: a submodule has changes of its own and was kept; to" \
      "discard them, git submodule deinit --force third_party/vcpkg" \
      "third_party/nvtx." >&2
    exit 1
  fi
  echo "bootstrap: the checkout is as cloned."
}

# Clones the repository and runs the clone's bootstrap, then fetches the LFS
# content the clone checked out as pointers (git-lfs wasn't installed yet).
bootstrap_from_pipe() {
  local dir="${AUGUSTA_DIR:-${PWD}/augusta}"
  if ! command -v git >/dev/null 2>&1; then
    # Only Linux can get here: Git Bash comes with git, macOS with Apple's.
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends git
  fi
  if [[ -d "${dir}/.git" ]]; then
    echo "bootstrap: ${dir} already exists - bootstrapping it as it is." >&2
  else
    git clone --branch develop "${REPOSITORY}" "${dir}"
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
    uninstall)
      case "${2:-}" in
        toolchain) uninstall_toolchain ;;
        checkout) unready_checkout ;;
        "")
          unready_checkout
          uninstall_toolchain
          ;;
        *) usage ;;
      esac
      ;;
    *) usage ;;
  esac
}

usage() {
  echo "usage: $0 [toolchain|checkout]" >&2
  echo "       $0 uninstall [toolchain|checkout]" >&2
  exit 2
}

main "$@"

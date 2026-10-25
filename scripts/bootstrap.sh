#!/bin/bash
# Bootstraps the development environment (docs/ENGINEERING.md, Developer
# Environment): the Linux/server one on Ubuntu 26.04, the Windows client one in
# Git Bash, and the macOS one on Apple silicon, native as Linux's. The one
# recipe for each: what differs lives in scripts/bootstrap/<platform>.sh, what
# they share here. .devcontainer's Dockerfile runs the Linux platform step and
# its post-create.sh the checkout step; a host runs both.
#
# Usage: scripts/bootstrap.sh [platform|checkout]
#        scripts/bootstrap.sh uninstall [platform|checkout]
#
# platform, once per machine, installs the system's packages and fills the
# toolchain's shared store (scripts/bootstrap/toolchain.sh). checkout readies
# the checkout, then runs `make toolchain`, which links its .toolchain/ into the
# store.
#
# Piped from curl outside a checkout (README.md, Bootstrap), it first clones
# the repository into ${AUGUSTA_DIR:-./augusta} and runs that clone's copy.
#
# uninstall undoes them: the checkout step's readying (.toolchain/, vcpkg's tool
# and binary cache, the hooks, the submodules; never the repository's files),
# and what the platform step installed that was not there before, the store
# among it, as its manifest records it, newest first. A machine bootstrapped before the manifest has
# none: what it installed then stays.
set -euo pipefail

readonly REPOSITORY=https://github.com/sergioffpc/augusta.git

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

# Installs the system's packages, then fills the toolchain's shared store,
# which the manifest records when it is new, for uninstall to remove.
ready_platform() {
  local toolchain store
  toolchain="$(dirname "${BASH_SOURCE[0]}")/bootstrap/toolchain.sh"
  install_platform
  store="$(bash "${toolchain}" store-dir)"
  [[ -e "${store}" ]] || manifest_add dir "${store}"
  bash "${toolchain}" store
}

# Undoes the manifest's entries, newest first, then removes it. The kinds
# every platform shares are undone here, the rest by its undo_entry.
uninstall_platform() {
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
  make toolchain
}

# Undoes ready_checkout but for LFS's filters, the platform's: the
# repository's files and any work in them stay. .toolchain/ goes, which holds
# links into the store and what make toolchain installed from it.
unready_checkout() {
  cd "$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
  if [[ "$(git config --get core.hooksPath || true)" = .githooks ]]; then
    git config --unset core.hooksPath
  fi
  unbootstrap_vcpkg
  rm -rf .toolchain
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
    platform) ready_platform ;;
    checkout) ready_checkout ;;
    "")
      ready_platform
      ready_checkout
      bootstrap_done
      ;;
    uninstall)
      case "${2:-}" in
        platform) uninstall_platform ;;
        checkout) unready_checkout ;;
        "")
          unready_checkout
          uninstall_platform
          ;;
        *) usage ;;
      esac
      ;;
    *) usage ;;
  esac
}

usage() {
  echo "usage: $0 [platform|checkout]" >&2
  echo "       $0 uninstall [platform|checkout]" >&2
  exit 2
}

main "$@"

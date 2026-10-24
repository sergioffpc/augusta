# shellcheck shell=bash
# The macOS half of scripts/bootstrap.sh (sourced by it), through Homebrew.
# Nothing builds natively on macOS: the host gets git, Git LFS and a container
# engine, and the toolchain lives in the dev container's image
# (scripts/dev-container.sh), which builds and readies the checkout inside.
# macOS's /bin/bash is 3.2, which runs this.

# Starts Podman's virtual machine, creating it first if there is none: Podman
# runs its Linux containers there. With every core and half the memory: the
# first build compiles the vcpkg dependencies in it. The machine mounts only
# the home directory (podman machine init's default), where the checkout is.
start_podman_machine() {
  if ! podman machine inspect >/dev/null 2>&1; then
    manifest_add podman-machine
    podman machine init --cpus "$(sysctl -n hw.ncpu)" \
      --memory "$(($(sysctl -n hw.memsize) / 2 / 1024 / 1024))"
  fi
  if [[ "$(podman machine inspect --format '{{.State}}')" != running ]]; then
    podman machine start
  fi
}

# The manifest (scripts/bootstrap.sh), the user's, as Homebrew's installs are.
manifest_file() {
  echo "${XDG_STATE_HOME:-${HOME}/.local/state}/augusta-bootstrap/manifest"
}

manifest_append() {
  mkdir -p "$(dirname "$(manifest_file)")"
  cat >>"$(manifest_file)"
}

remove_manifest() {
  rm -rf "$(dirname "$(manifest_file)")"
}

require_uninstall_rights() {
  :
}

# Installs Homebrew formulae, recording those that were not installed.
# Arguments:
#   The formulae.
brew_install() {
  local formula
  for formula in "$@"; do
    brew list --formula "${formula}" >/dev/null 2>&1 \
      || manifest_add brew "${formula}"
  done
  brew install "$@"
}

# Prints the container engine scripts/dev-container.sh takes.
container_engine() {
  if command -v podman >/dev/null 2>&1; then
    echo podman
  else
    echo docker
  fi
}

# Removes every checkout's dev containers, their images and the volumes they
# share, which scripts/dev-container.sh made for the bootstrap.
remove_dev_containers() {
  local engine
  engine="$(container_engine)"
  command -v "${engine}" >/dev/null 2>&1 || return 0
  remove_listed "${engine}" "ps --all --quiet --filter name=augusta-dev-" rm
  remove_listed "${engine}" "images --quiet augusta-dev" rmi
  remove_listed "${engine}" "volume ls --quiet --filter name=augusta-" \
    "volume rm"
}

# Removes, with --force, what an engine's listing prints, one id per line.
# Arguments:
#   The engine, the listing's arguments, the removal's arguments.
remove_listed() {
  local ids=() id listing removal
  read -r -a listing <<<"$2"
  read -r -a removal <<<"$3"
  while IFS= read -r id; do
    [[ -n "${id}" ]] && ids+=("${id}")
  done < <("$1" "${listing[@]}")
  ((${#ids[@]} == 0)) || "$1" "${removal[@]}" --force "${ids[@]}" >/dev/null
}

# Undoes a manifest entry of this platform's.
# Arguments:
#   Its kind, then its fields.
undo_entry() {
  case "$1" in
    brew) brew uninstall "$2" ;;
    podman-machine) podman machine rm --force ;;
    dev-containers) remove_dev_containers ;;
    *) echo "bootstrap: no way to undo $1 here; skipped." >&2 ;;
  esac
}

install_toolchain() {
  if ! command -v brew >/dev/null 2>&1; then
    echo "bootstrap: Homebrew not found - install it (https://brew.sh) and" \
      "re-run this script." >&2
    exit 1
  fi
  # Apple's git (Xcode's Command Line Tools) has no Git LFS beside it.
  brew_install git git-lfs
  # An existing Docker is left as it is: scripts/dev-container.sh takes it
  # where there is no Podman.
  if ! command -v podman >/dev/null 2>&1 && command -v docker >/dev/null 2>&1; then
    if ! docker info >/dev/null 2>&1; then
      echo "bootstrap: docker is installed but not running - start it (Docker" \
        "Desktop, Colima, ...) and re-run this script." >&2
      exit 1
    fi
  else
    brew_install podman
    start_podman_machine
  fi
}

# The submodules the Linux build uses, as scripts/bootstrap/linux.sh's: git
# runs on the host, with its credentials.
update_submodules() {
  git submodule update --init third_party/vcpkg third_party/nvtx
}

# Builds the dev container's image and creates the container, whose
# post-create (.devcontainer/post-create.sh) bootstraps vcpkg inside it, for
# Linux.
bootstrap_vcpkg() {
  manifest_add dev-containers
  scripts/dev-container.sh true
}

# Removes this checkout's container, and what its post-create's Linux
# bootstrap_vcpkg wrote in the checkout: vcpkg's tool and the binary cache.
unbootstrap_vcpkg() {
  scripts/dev-container.sh --remove
  rm -rf third_party/vcpkg/vcpkg .vcpkg-bincache
}

bootstrap_done() {
  echo "bootstrap: done. Run 'scripts/dev-container.sh' for a shell in the" \
    "dev container, and once 'scripts/dev-container.sh make configure" \
    "PRESET=linux-debug' so the pre-push hook can run clang-tidy."
}

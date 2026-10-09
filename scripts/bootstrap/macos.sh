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
    podman machine init --cpus "$(sysctl -n hw.ncpu)" \
      --memory "$(($(sysctl -n hw.memsize) / 2 / 1024 / 1024))"
  fi
  if [[ "$(podman machine inspect --format '{{.State}}')" != running ]]; then
    podman machine start
  fi
}

install_toolchain() {
  if ! command -v brew >/dev/null 2>&1; then
    echo "bootstrap: Homebrew not found - install it (https://brew.sh) and" \
      "re-run this script." >&2
    exit 1
  fi
  # Apple's git (Xcode's Command Line Tools) has no Git LFS beside it.
  brew install git git-lfs
  # An existing Docker is left as it is: scripts/dev-container.sh prefers it.
  if command -v docker >/dev/null 2>&1; then
    if ! docker info >/dev/null 2>&1; then
      echo "bootstrap: docker is installed but not running - start it (Docker" \
        "Desktop, Colima, ...) and re-run this script." >&2
      exit 1
    fi
  else
    brew install podman
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
  scripts/dev-container.sh true
}

bootstrap_done() {
  echo "bootstrap: done. Run 'scripts/dev-container.sh' for a shell in the" \
    "dev container, and once 'scripts/dev-container.sh make configure" \
    "PRESET=linux-debug' so the pre-push hook can run clang-tidy."
}

#!/bin/bash
# Runs a command in this checkout's Linux dev container (docs/ENGINEERING.md,
# Developer Environment): the environment of .devcontainer/, from a terminal,
# for a Linux build on a host with its own (macOS builds natively). git itself
# (identity, signing, credentials, LFS) stays on the host.
#
# One container per checkout, kept running between calls. The checkout is
# mounted at the path it has on the host, not /workspaces/<name> as
# devcontainer.json mounts it, so a git worktree's .git (an absolute host
# path) resolves inside too, as does its main repository's, mounted beside
# it. Its build/ and .toolchain/ are volumes of its own, as devcontainer.json's
# (the host's .toolchain/ links into the host's store); sccache's cache and
# vcpkg's binary cache, downloads and build trees are volumes every checkout
# shares, so a new worktree doesn't rebuild the vcpkg dependencies.
# The image is rebuilt, and the container recreated, when the Dockerfile,
# scripts/bootstrap.sh, scripts/bootstrap/linux.sh or the toolchain's files it
# copies change (not the Windows half): the image fills the toolchain's store.
#
# Podman, or Docker where there is no Podman (rootless Podman maps the host's
# user to the image's ubuntu, so files written inside are the host user's).
#
# Usage: scripts/dev-container.sh [command [argument...]]
#   With no command, an interactive shell. Runs in the current directory.
#        scripts/dev-container.sh --remove
#   Removes this checkout's container and its build/ and .toolchain/ volumes
#   (the bootstrap's uninstall), leaving the images and the volumes every
#   checkout shares.
set -euo pipefail

# The git environment a hook runs with, which a command run from one needs
# too: a commit's index can be a temporary file.
readonly GIT_ENV=(GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE)
# Inside the image (.devcontainer/Dockerfile): its user's home.
readonly HOME_IN=/home/ubuntu
readonly VCPKG_CACHE="${HOME_IN}/.cache/augusta-vcpkg"
# The files the image's build copies (.devcontainer/Dockerfile.dockerignore),
# whose change rebuilds it.
readonly IMAGE_FILES=(.devcontainer/Dockerfile scripts/bootstrap.sh
  scripts/bootstrap/linux.sh scripts/bootstrap/toolchain.sh
  scripts/bootstrap/versions.env scripts/bootstrap/package.json
  scripts/bootstrap/package-lock.json)

# Prints the container engine: podman if there is one, docker otherwise.
engine() {
  local candidate
  for candidate in podman docker; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      echo "${candidate}"
      return
    fi
  done
  echo "dev-container: neither podman nor docker found - install one (on" \
    "macOS: brew install podman && podman machine init && podman machine" \
    "start)." >&2
  exit 1
}

# Prints a short, stable identifier of its argument.
digest() {
  cksum | cut -d ' ' -f 1
}

# Prints the name of a checkout's volume for one of its directories.
# Arguments:
#   The directory (build, toolchain), the checkout.
checkout_volume() {
  echo "augusta-$1-$(printf '%s' "$2" | digest)"
}

# Builds the image tagged $1 from the checkout $2, unless it exists.
# Arguments:
#   The engine, the tag, the checkout.
build_image() {
  local engine="$1" tag="$2" top="$3"
  "${engine}" image inspect "${tag}" >/dev/null 2>&1 && return
  echo "dev-container: building ${tag} (once per change to the Dockerfile" \
    "or the Linux bootstrap)..." >&2
  local ignore=()
  # Docker reads Dockerfile.dockerignore beside the Dockerfile by itself.
  [[ "${engine}" = podman ]] \
    && ignore=(--ignorefile "${top}/.devcontainer/Dockerfile.dockerignore")
  # Expanded only when set: macOS's bash 3.2 takes an empty array for unbound.
  "${engine}" build ${ignore[@]+"${ignore[@]}"} -f "${top}/.devcontainer/Dockerfile" \
    -t "${tag}" "${top}"
}

# Creates and readies the container named $2, as devcontainer.json's
# post-create readies its own.
# Arguments:
#   The engine, the container, the image, the checkout, its git common dir.
create_container() {
  local engine="$1" name="$2" tag="$3" top="$4" common="$5"
  local args=(
    run --detach --init --name "${name}" --label "augusta.image=${tag}"
    --volume "${top}:${top}"
    --volume "$(checkout_volume build "${top}"):${top}/build"
    --volume "$(checkout_volume toolchain "${top}"):${top}/.toolchain"
    --volume "augusta-sccache:${HOME_IN}/.cache/sccache"
    --volume "augusta-vcpkg:${VCPKG_CACHE}"
    --env "VCPKG_BINARY_SOURCES=clear;files,${VCPKG_CACHE}/bincache,readwrite"
    --workdir "${top}"
  )
  # A worktree's repository lives in its main checkout.
  [[ "${common}" = "${top}"/* ]] || args+=(--volume "${common}:${common}")
  [[ "${engine}" = podman ]] && args+=("--userns=keep-id:uid=1000,gid=1000")
  echo "dev-container: creating ${name} for ${top}..." >&2
  "${engine}" "${args[@]}" "${tag}" sleep infinity >/dev/null
  # A volume mounts in owned by root where the image has no directory.
  # shellcheck disable=SC2016 # expanded inside, as the image's user
  "${engine}" exec "${name}" bash -c \
    'sudo chown "$(id -u):$(id -g)" "$1" && mkdir -p "$1/bincache"' \
    bash "${VCPKG_CACHE}"
  "${engine}" exec --workdir "${top}" "${name}" \
    bash .devcontainer/post-create.sh
}

main() {
  local top common engine
  # From where this script is, not git: a hook's GIT_DIR would make git take
  # the current directory for the checkout.
  top="$(cd "$(dirname "$0")/.." && pwd -P)"
  common="$(
    unset GIT_DIR GIT_WORK_TREE
    git -C "${top}" rev-parse --path-format=absolute --git-common-dir
  )"
  engine="$(engine)"

  local tag name
  tag="augusta-dev:$(cd "${top}" && cat "${IMAGE_FILES[@]}" | digest)"
  name="augusta-dev-$(printf '%s' "${top}" | digest)"

  if [[ "${1:-}" = --remove ]]; then
    "${engine}" rm --force "${name}" >/dev/null 2>&1 || true
    "${engine}" volume rm --force "$(checkout_volume build "${top}")" \
      "$(checkout_volume toolchain "${top}")" >/dev/null 2>&1 || true
    echo "dev-container: removed ${name} and its build and toolchain" \
      "volumes." >&2
    return
  fi

  build_image "${engine}" "${tag}" "${top}"
  local current
  current="$("${engine}" container inspect --format \
    '{{index .Config.Labels "augusta.image"}}' "${name}" 2>/dev/null || true)"
  if [[ -n "${current}" && "${current}" != "${tag}" ]]; then
    echo "dev-container: ${name} runs an older image - recreating it." >&2
    "${engine}" rm --force "${name}" >/dev/null
    current=""
  fi
  if [[ -z "${current}" ]]; then
    create_container "${engine}" "${name}" "${tag}" "${top}" "${common}"
  else
    "${engine}" start "${name}" >/dev/null
  fi

  local exec_args=(exec --interactive --workdir "${PWD}")
  [[ -t 0 && -t 1 ]] && exec_args+=(--tty)
  local variable
  for variable in "${GIT_ENV[@]}"; do
    [[ -n "${!variable:-}" ]] && exec_args+=(--env "${variable}=${!variable}")
  done
  (($# > 0)) || set -- bash
  exec "${engine}" "${exec_args[@]}" "${name}" "$@"
}

main "$@"

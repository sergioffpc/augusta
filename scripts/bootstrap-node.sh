#!/bin/bash
# Bootstraps the k3s node that runs the develop and staging environments
# (ADR-0026), on Debian 13, as a user that can sudo: the crash-core handler
# and the tools that read a core, the server's user, the asset-pack volume and
# the capture filesystem
# (docs/runbooks/k3s-node-recovery.md), k3s at the cluster's release with its
# hardened configuration (clusters/onprem/node/, ADR-0057), and Flux from the
# repository's own manifests, which then installs everything else
# (clusters/onprem/). Each step is skipped when the node already has it, so a
# re-run finishes an interrupted one; k3s's configuration is written again
# each time, and a k3s already running takes it when it next restarts.
#
# It never writes the asset packs' contents: they are the one state not in Git.
# --packs puts back an archive taken from a node (the runbook's part B), else
# augusta-publish fills the volume.
#
# Usage: scripts/bootstrap-node.sh [--packs <asset-packs.tgz>]
#   Piped from curl: curl ... | bash -s -- [--packs <asset-packs.tgz>]

# POSIX, so that sh (dash on Debian) gets here: it would run the rest with
# every [[ ]] failing, which skips steps instead of stopping.
# shellcheck disable=SC2292 # [[ ]] is what sh lacks
if [ -z "${BASH_VERSION:-}" ]; then
  if [ -f "$0" ]; then
    exec bash "$0" "$@"
  fi
  echo "bootstrap-node: run it with bash, not sh." >&2
  exit 2
fi
set -euo pipefail

# The cluster's k3s release: its Kubernetes version is CI's
# KUBERNETES_VERSION (.github/workflows/ci.yml), which the manifests are
# validated against, and CI fails when they differ. Bump them together
# (docs/runbooks/k3s-node-recovery.md, Upgrade k3s).
readonly K3S_VERSION=v1.36.4+k3s1
# What the flux-system GitRepository tracks (clusters/onprem/flux-system/).
readonly MANIFESTS=https://raw.githubusercontent.com/sergioffpc/augusta/develop/clusters/onprem/flux-system
# k3s's configuration, from the same branch.
readonly NODE_CONFIG=https://raw.githubusercontent.com/sergioffpc/augusta/develop/clusters/onprem/node
readonly K3S_CONFIG_FILES=(config.yaml audit-policy.yaml admission.yaml)
readonly SYSCTL_FILE=90-kubelet.conf
# The server image's user (ADR-0054), which the node has too.
readonly SERVER_USER=augusta SERVER_ID=65532
readonly VOLUME=/srv/augusta
readonly CAPTURE_IMAGE="${VOLUME}/captures.img" CAPTURE_SIZE=4GiB
readonly CAPTURE_FSTAB="${CAPTURE_IMAGE} ${VOLUME}/captures ext4 loop,nodev,nosuid,noexec,x-systemd.before=k3s.service 0 2"

log() {
  echo "bootstrap-node: $*" >&2
}

# Runs its arguments as root: directly when already root, through sudo
# otherwise.
as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

kubectl() {
  as_root k3s kubectl "$@"
}

check_os() {
  # shellcheck source=/dev/null
  . /etc/os-release
  if [[ "${ID}" != debian || "${VERSION_ID}" != 13 ]]; then
    log "${PRETTY_NAME} is not Debian 13, the node's release - continuing."
  fi
}

install_packages() {
  # systemd-coredump: the kernel's core handler, which keeps a crashed
  # server's core (ADR-0047). podman and gdb read it on the node: podman takes
  # the binary and its debug info out of the sha-<12> and sha-<12>-debuginfo
  # images, daemonless; k3s runs its pods on its own containerd and needs
  # neither podman nor Docker.
  as_root apt-get update
  as_root apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    e2fsprogs \
    gdb \
    podman \
    systemd-coredump \
    util-linux
}

create_server_user() {
  if ! getent group "${SERVER_USER}" >/dev/null; then
    as_root groupadd --system --gid "${SERVER_ID}" "${SERVER_USER}"
  fi
  if ! getent passwd "${SERVER_USER}" >/dev/null; then
    as_root useradd --system --uid "${SERVER_ID}" --gid "${SERVER_USER}" \
      --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin \
      "${SERVER_USER}"
  fi
}

# Creates the asset-pack volume, and fills it from an archive if given one.
# Arguments:
#   The archive of asset-packs/, or nothing.
create_pack_volume() {
  local packs="${VOLUME}/asset-packs"
  # Only when missing: an existing volume keeps the modes its servers read it
  # with.
  if [[ ! -d "${packs}" ]]; then
    as_root install -d -o root -g root -m 0755 "${VOLUME}"
    as_root install -d -o root -g "${SERVER_USER}" -m 0750 "${packs}"
  fi
  if [[ -n "${1:-}" ]]; then
    if [[ -n "$(as_root find "${packs}" -mindepth 1 -print -quit)" ]]; then
      log "${packs} is not empty - leaving it, not unpacking $1."
      return
    fi
    # As root, tar keeps the archive's owners and modes.
    as_root tar -C "${VOLUME}" -xzf "$1" asset-packs
  fi
}

create_capture_filesystem() {
  local mount="${VOLUME}/captures"
  as_root install -d -o root -g root -m 0755 "${mount}"
  if [[ ! -f "${CAPTURE_IMAGE}" ]]; then
    as_root fallocate -l "${CAPTURE_SIZE}" "${CAPTURE_IMAGE}"
    as_root chmod 0600 "${CAPTURE_IMAGE}"
    as_root mkfs.ext4 -q -m 0 -L augusta-captures "${CAPTURE_IMAGE}"
  fi
  if ! grep -qF "${CAPTURE_IMAGE} " /etc/fstab; then
    echo "${CAPTURE_FSTAB}" | as_root tee -a /etc/fstab >/dev/null
    as_root systemctl daemon-reload
  fi
  if ! findmnt --mountpoint "${mount}" >/dev/null; then
    as_root mount "${mount}"
  fi
  as_root install -d -o "${SERVER_USER}" -g "${SERVER_USER}" -m 0755 "${mount}"
}

# Arguments:
#   A downloaded file, the file it is to replace.
# Returns:
#   0 when they differ or the second is missing, 1 when they are the same.
differs() {
  ! as_root cmp -s "$1" "$2"
}

# Writes k3s's configuration, its audit policy and admission configuration
# (clusters/onprem/node/), and the kernel parameters its kubelet needs, which
# are applied at once: protect-kernel-defaults stops a kubelet that starts
# without them.
configure_k3s() {
  local downloads changed="" file
  downloads="$(mktemp -d)"
  for file in "${K3S_CONFIG_FILES[@]}" "${SYSCTL_FILE}"; do
    curl -fsSL "${NODE_CONFIG}/${file}" -o "${downloads}/${file}"
  done

  if differs "${downloads}/${SYSCTL_FILE}" "/etc/sysctl.d/${SYSCTL_FILE}"; then
    changed=yes
  fi
  as_root install -d -o root -g root -m 0755 /etc/sysctl.d
  as_root install -o root -g root -m 0644 "${downloads}/${SYSCTL_FILE}" \
    /etc/sysctl.d
  as_root sysctl --quiet --load "/etc/sysctl.d/${SYSCTL_FILE}"

  as_root install -d -o root -g root -m 0755 /etc/rancher/k3s
  for file in "${K3S_CONFIG_FILES[@]}"; do
    if differs "${downloads}/${file}" "/etc/rancher/k3s/${file}"; then
      changed=yes
    fi
    as_root install -o root -g root -m 0600 "${downloads}/${file}" \
      /etc/rancher/k3s
  done
  # The audit log's, before the API server first writes it: root's only, as
  # the log holds Secrets' contents. The server directory is 0700 as k3s
  # makes it.
  as_root install -d -o root -g root -m 0700 /var/lib/rancher/k3s/server \
    /var/lib/rancher/k3s/server/logs
  rm -rf "${downloads}"

  if [[ -n "${changed}" ]] && command -v k3s >/dev/null 2>&1; then
    log "k3s's configuration changed; the running k3s takes it when it" \
      "restarts. See docs/runbooks/k3s-node-recovery.md, Change k3s's" \
      "configuration."
  fi
}

install_k3s() {
  if command -v k3s >/dev/null 2>&1; then
    local installed
    installed="$(k3s --version | awk 'NR == 1 { print $3 }')"
    if [[ "${installed}" != "${K3S_VERSION}" ]]; then
      log "k3s ${installed} is installed, not ${K3S_VERSION} - leaving it." \
        "See docs/runbooks/k3s-node-recovery.md, Upgrade k3s."
    fi
  else
    curl -sfL https://get.k3s.io \
      | as_root env INSTALL_K3S_VERSION="${K3S_VERSION}" sh -
  fi
  # The node object appears a few seconds after k3s starts.
  until kubectl get nodes 2>/dev/null | grep -q .; do
    sleep 2
  done
  kubectl wait --for=condition=Ready node --all --timeout=5m
}

# Installs Flux's controllers, then the sync objects that point it at the
# repository; from there Flux manages itself and installs the rest.
install_flux() {
  # Once synced, Flux owns its own manifests: applying them again from here
  # conflicts with its kustomize-controller over their fields.
  if kubectl -n flux-system get gitrepository flux-system >/dev/null 2>&1; then
    log "Flux is already installed and syncing - leaving it."
    return
  fi
  curl -fsSL "${MANIFESTS}/gotk-components.yaml" \
    | kubectl apply --server-side -f -
  kubectl wait --for=condition=Established crd --all --timeout=2m
  kubectl -n flux-system wait --for=condition=Available deployment --all \
    --timeout=5m
  curl -fsSL "${MANIFESTS}/gotk-sync.yaml" | kubectl apply -f -
}

main() {
  local packs=""
  case "${1:-}" in
    --packs)
      if (($# != 2)) || [[ ! -f "$2" ]]; then
        echo "usage: $0 [--packs <asset-packs.tgz>]" >&2
        exit 2
      fi
      packs="$(realpath "$2")"
      ;;
    "") ;;
    *)
      echo "usage: $0 [--packs <asset-packs.tgz>]" >&2
      exit 2
      ;;
  esac

  check_os
  install_packages
  create_server_user
  create_pack_volume "${packs}"
  create_capture_filesystem
  configure_k3s
  install_k3s
  install_flux

  log "done. Flux installs the monitoring stack and both environments; watch" \
    "with: sudo k3s kubectl get pods -A. A server whose pack isn't on the" \
    "node waits in ContainerCreating until augusta-publish puts it there."
}

main "$@"

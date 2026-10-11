#!/bin/bash
# Bootstraps the k3s node that runs the develop and staging environments
# (ADR-0026), on Debian 13, as a user that can sudo: the crash-core handler
# and the tools that read a core, the server's user, the asset-pack volume and
# the capture filesystem
# (docs/runbooks/k3s-node-recovery.md), the OS's hardening (a firewall, SSH by
# key only, unattended security updates, an audit trail), k3s at the cluster's
# release with its hardened configuration (clusters/onprem/node/, ADR-0057),
# and Flux from the repository's own manifests, which then installs everything
# else (clusters/onprem/). Each step is skipped when the node already has it,
# so a re-run finishes an interrupted one; the node's configuration is written
# again each time, and a k3s already running takes its own when it next
# restarts.
#
# The firewall lets only the maintainer's addresses reach SSH and the API
# server. They are the node's, not the repository's: --admin gives them (an
# address or a range, once each), replacing the node's list; without it a new
# node takes the address of the SSH session running this. A run that would
# leave that session's address out stops, as does one by a user SSH would no
# longer let in (root, or a user without a key).
#
# It never writes the asset packs' contents: they are the one state not in Git.
# --packs puts back an archive taken from a node (the runbook's part B), else
# augusta-publish fills the volume.
#
# Usage: scripts/bootstrap-node.sh [--packs <asset-packs.tgz>] [--admin <address>]...
#   Piped from curl: curl ... | bash -s -- [--packs <asset-packs.tgz>] [--admin <address>]...

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
# k3s's configuration, from the same branch: the files the runbook's "The k3s
# configuration" lists, kept in step with it.
readonly NODE_CONFIG=https://raw.githubusercontent.com/sergioffpc/augusta/develop/clusters/onprem/node
readonly K3S_CONFIG_FILES=(config.yaml audit-policy.yaml admission.yaml)
readonly SYSCTL_FILE=90-kubelet.conf
# The OS's, from the same place: the files the runbook's "The node's OS" lists.
readonly OS_FILES=(nftables.conf nftables.service.conf sshd.conf
  unattended-upgrades.conf apt-daily-upgrade.timer.conf audit.rules)
readonly ADMIN_ADDRESSES=/etc/nftables.d/admin-addresses.nft
# What k3s secrets-encrypt enable, or a first start with secrets-encryption,
# writes.
readonly ENCRYPTION_CONFIG=/var/lib/rancher/k3s/server/cred/encryption-config.json
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
    auditd \
    ca-certificates \
    curl \
    e2fsprogs \
    gdb \
    nftables \
    openssh-server \
    podman \
    systemd-coredump \
    unattended-upgrades \
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

# Downloads the node's configuration (clusters/onprem/node/) into DOWNLOADS,
# removed when the script exits.
download_node_config() {
  local file
  DOWNLOADS="$(mktemp -d)"
  # shellcheck disable=SC2064 # DOWNLOADS is set once, here
  trap "rm -rf -- '${DOWNLOADS}'" EXIT
  for file in "${K3S_CONFIG_FILES[@]}" "${SYSCTL_FILE}" "${OS_FILES[@]}"; do
    curl -fsSL "${NODE_CONFIG}/${file}" -o "${DOWNLOADS}/${file}"
  done
}

# Arguments:
#   A downloaded file, the file it is to replace.
# Returns:
#   0 when they differ or the second is missing, 1 when they are the same.
differs() {
  ! as_root cmp -s "$1" "$2"
}

# Installs a downloaded file where the node reads it, root's, unless the node
# has it already.
# Arguments:
#   Its name in clusters/onprem/node/, its path on the node, its mode.
# Returns:
#   0 when it was written, 1 when the node already had it.
install_node_file() {
  if ! differs "${DOWNLOADS}/$1" "$2"; then
    return 1
  fi
  # Called as a condition, a failure here would not stop the script by itself.
  if ! as_root install -D -o root -g root -m "$3" "${DOWNLOADS}/$1" "$2"; then
    log "cannot write $2."
    exit 1
  fi
}

# Prints the address of the SSH session running this, if any.
ssh_client_address() {
  local address="${SSH_CONNECTION:-}"
  address="${address%% *}"
  echo "${address#::ffff:}"
}

# Writes the maintainer's addresses, which the firewall alone lets reach SSH
# and the API server, unless the node has them and none is given.
# Arguments:
#   The addresses or ranges, or nothing.
write_admin_addresses() {
  local addresses=("$@") address set
  if (($# == 0)); then
    if as_root test -e "${ADMIN_ADDRESSES}"; then
      return
    fi
    address="$(ssh_client_address)"
    if [[ -z "${address}" ]]; then
      log "no admin address: give the maintainer's with --admin."
      exit 1
    fi
    addresses=("${address}")
  fi
  {
    echo "# The maintainer's fixed addresses: scripts/bootstrap-node.sh --admin."
    for address in "${addresses[@]}"; do
      set=admin4
      if [[ "${address}" == *:* ]]; then
        set=admin6
      fi
      echo "add element inet augusta ${set} { ${address} }"
    done
  } >"${DOWNLOADS}/admin-addresses.nft"
  as_root install -D -o root -g root -m 0644 \
    "${DOWNLOADS}/admin-addresses.nft" "${ADMIN_ADDRESSES}"
}

# Loads the firewall, and has it loaded at every boot. A run from an SSH
# session the firewall would not let in again takes it back down and stops.
# Arguments:
#   The maintainer's addresses, or nothing.
configure_firewall() {
  local address
  write_admin_addresses "$@"
  as_root nft --check --file "${DOWNLOADS}/nftables.conf"
  install_node_file nftables.conf /etc/nftables.conf 0644 || true
  if install_node_file nftables.service.conf \
    /etc/systemd/system/nftables.service.d/augusta.conf 0644; then
    as_root systemctl daemon-reload
  fi
  as_root systemctl enable --quiet nftables
  as_root systemctl reload-or-restart nftables

  address="$(ssh_client_address)"
  if [[ -n "${address}" ]] \
    && ! as_root nft get element inet augusta admin4 "{ ${address} }" \
      >/dev/null 2>&1 \
    && ! as_root nft get element inet augusta admin6 "{ ${address} }" \
      >/dev/null 2>&1; then
    as_root nft delete table inet augusta
    log "this session's address, ${address}, is not an admin address, and" \
      "SSH would refuse it from now on - the firewall is down again. Give" \
      "it with --admin, along with the others."
    exit 1
  fi
}

# Has SSH take keys only, and no root login. When that changes, the user
# running this must log in by key already, or SSH would lock them out.
configure_ssh() {
  local drop_in=/etc/ssh/sshd_config.d/10-augusta.conf
  local user="${SUDO_USER:-$(id -un)}" home
  if ! differs "${DOWNLOADS}/sshd.conf" "${drop_in}"; then
    return
  fi
  home="$(getent passwd "${user}" | cut -d : -f 6)"
  if [[ "${user}" == root ]] \
    || ! as_root test -s "${home}/.ssh/authorized_keys"; then
    log "SSH is about to take keys only, and never root's, which would lock" \
      "${user} out: run it as the user you log in with, once their key is in" \
      ".ssh/authorized_keys. See docs/runbooks/k3s-node-recovery.md, SSH."
    exit 1
  fi
  install_node_file sshd.conf "${drop_in}" 0644 || true
  if ! as_root sshd -t; then
    as_root rm "${drop_in}"
    log "sshd rejects its configuration - left as it was."
    exit 1
  fi
  as_root systemctl reload-or-restart ssh
}

# Has Debian's security updates install every night, rebooting at 05:00 when
# one needs it.
configure_updates() {
  install_node_file unattended-upgrades.conf \
    /etc/apt/apt.conf.d/52augusta-upgrades 0644 || true
  if install_node_file apt-daily-upgrade.timer.conf \
    /etc/systemd/system/apt-daily-upgrade.timer.d/augusta.conf 0644; then
    as_root systemctl daemon-reload
  fi
  as_root systemctl enable --quiet --now apt-daily.timer \
    apt-daily-upgrade.timer
}

# Has auditd record logins, privilege changes and changes to k3s's
# configuration.
configure_audit() {
  # A watched directory must exist when the rules load; configure_k3s, which
  # fills it, may not have run yet.
  as_root install -d -o root -g root -m 0755 /etc/rancher/k3s
  as_root systemctl enable --quiet --now auditd
  if install_node_file audit.rules /etc/audit/rules.d/augusta.rules 0640; then
    as_root augenrules --load
  fi
}

# Writes k3s's configuration, its audit policy and admission configuration
# (clusters/onprem/node/), and the kernel parameters its kubelet needs, which
# are applied at once: protect-kernel-defaults stops a kubelet that starts
# without them.
configure_k3s() {
  local changed="" file
  # A node installed before its configuration was in the repository runs
  # without secrets encryption prepared; written now, the configuration's
  # secrets-encryption would take it unprepared at k3s's next restart.
  if command -v k3s >/dev/null 2>&1 \
    && ! as_root test -e "${ENCRYPTION_CONFIG}"; then
    log "k3s runs without secrets encryption prepared - leaving its" \
      "configuration. See docs/runbooks/k3s-node-recovery.md, Harden a" \
      "running node."
    exit 1
  fi

  if differs "${DOWNLOADS}/${SYSCTL_FILE}" "/etc/sysctl.d/${SYSCTL_FILE}"; then
    changed=yes
  fi
  as_root install -d -o root -g root -m 0755 /etc/sysctl.d
  as_root install -o root -g root -m 0644 "${DOWNLOADS}/${SYSCTL_FILE}" \
    /etc/sysctl.d
  as_root sysctl --quiet --load "/etc/sysctl.d/${SYSCTL_FILE}"

  as_root install -d -o root -g root -m 0755 /etc/rancher/k3s
  for file in "${K3S_CONFIG_FILES[@]}"; do
    if differs "${DOWNLOADS}/${file}" "/etc/rancher/k3s/${file}"; then
      changed=yes
    fi
    as_root install -o root -g root -m 0600 "${DOWNLOADS}/${file}" \
      /etc/rancher/k3s
  done
  # The audit log's, before the API server first writes it: root's only, as
  # the log holds Secrets' contents. The server directory is 0700 as k3s
  # makes it.
  as_root install -d -o root -g root -m 0700 /var/lib/rancher/k3s/server \
    /var/lib/rancher/k3s/server/logs

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

usage() {
  echo "usage: $0 [--packs <asset-packs.tgz>] [--admin <address>]..." >&2
  exit 2
}

main() {
  local packs="" admins=()
  while (($# > 0)); do
    case "$1" in
      --packs)
        if (($# < 2)) || [[ ! -f "$2" ]]; then
          usage
        fi
        packs="$(realpath "$2")"
        ;;
      --admin)
        if (($# < 2)) || [[ -z "$2" ]]; then
          usage
        fi
        admins+=("$2")
        ;;
      *) usage ;;
    esac
    shift 2
  done

  check_os
  install_packages
  download_node_config
  create_server_user
  create_pack_volume "${packs}"
  create_capture_filesystem
  configure_firewall "${admins[@]}"
  configure_ssh
  configure_updates
  configure_audit
  configure_k3s
  install_k3s
  install_flux

  log "done. Flux installs the monitoring stack and both environments; watch" \
    "with: sudo k3s kubectl get pods -A. A server whose pack isn't on the" \
    "node waits in ContainerCreating until augusta-publish puts it there."
}

main "$@"

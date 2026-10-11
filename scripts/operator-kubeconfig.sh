#!/bin/bash
# Issues, or renews, the maintainer's day-to-day kubeconfig for the k3s node
# (ADR-0057; docs/runbooks/k3s-node-recovery.md, Cluster access), on the
# workstation: a new key, which never leaves it, and a client certificate in
# the group augusta:operators (clusters/onprem/infrastructure/operator.yaml)
# that expires in a day. The node signs it through Kubernetes' certificate
# API, approved there with k3s's own cluster-admin, over one SSH login as a
# user with sudo. It replaces the kubeconfig in place, an expired one included.
#
# Usage: scripts/operator-kubeconfig.sh <node> [<kubeconfig>]
#   <node> is the SSH destination; the API server is at its host name, port
#   6443. <kubeconfig> defaults to ~/.kube/augusta.yaml.
set -euo pipefail

readonly LIFETIME_SECONDS=86400
readonly GROUP=augusta:operators

log() {
  echo "operator-kubeconfig: $*" >&2
}

# The script run on the node: submits the request, approves it, and prints the
# certificate, then the cluster's CA, each base64 on a line.
# Arguments:
#   The request's name, the CSR (PEM, base64).
remote_script() {
  cat <<EOF
set -euo pipefail
kubectl() { sudo k3s kubectl "\$@"; }
kubectl delete csr '$1' --ignore-not-found >/dev/null
kubectl apply -f - >/dev/null <<YAML
apiVersion: certificates.k8s.io/v1
kind: CertificateSigningRequest
metadata:
  name: $1
spec:
  request: $2
  signerName: kubernetes.io/kube-apiserver-client
  expirationSeconds: ${LIFETIME_SECONDS}
  usages: [client auth]
YAML
kubectl certificate approve '$1' >/dev/null
for _ in \$(seq 30); do
  certificate="\$(kubectl get csr '$1' -o jsonpath='{.status.certificate}')"
  [[ -n "\${certificate}" ]] && break
  sleep 1
done
kubectl delete csr '$1' >/dev/null
[[ -n "\${certificate}" ]] || { echo "the request was not signed" >&2; exit 1; }
echo "\${certificate}"
kubectl config view --raw -o jsonpath='{.clusters[0].cluster.certificate-authority-data}'
echo
EOF
}

main() {
  if (($# < 1 || $# > 2)); then
    echo "usage: $0 <node> [<kubeconfig>]" >&2
    exit 2
  fi
  local node="$1" kubeconfig="${2:-${HOME}/.kube/augusta.yaml}"
  local user host work name response certificate ca
  user="$(id -un)"
  host="$(ssh -G "${node}" | awk '$1 == "hostname" { print $2 }')"
  name="augusta-operator-${user}-$(date +%s)"
  work="$(mktemp -d)"
  # shellcheck disable=SC2064 # work is set once, here
  trap "rm -rf -- '${work}'" EXIT

  openssl ecparam -name prime256v1 -genkey -noout -out "${work}/key.pem"
  openssl req -new -key "${work}/key.pem" -subj "/CN=${user}/O=${GROUP}" \
    -out "${work}/csr.pem"

  log "asking ${node} to sign ${user}'s certificate..."
  response="$(remote_script "${name}" "$(base64 <"${work}/csr.pem" | tr -d '\n')" \
    | ssh "${node}" bash -s)"
  certificate="$(sed -n 1p <<<"${response}")"
  ca="$(sed -n 2p <<<"${response}")"

  umask 077
  mkdir -p "$(dirname "${kubeconfig}")"
  cat >"${work}/kubeconfig" <<EOF
apiVersion: v1
kind: Config
clusters:
  - name: augusta
    cluster:
      server: https://${host}:6443
      certificate-authority-data: ${ca}
users:
  - name: ${user}
    user:
      client-certificate-data: ${certificate}
      client-key-data: $(base64 <"${work}/key.pem" | tr -d '\n')
contexts:
  - name: augusta
    context:
      cluster: augusta
      user: ${user}
current-context: augusta
EOF
  mv "${work}/kubeconfig" "${kubeconfig}"
  log "wrote ${kubeconfig}, valid until" \
    "$(base64 -d <<<"${certificate}" | openssl x509 -noout -enddate | cut -d = -f 2)."
}

main "$@"

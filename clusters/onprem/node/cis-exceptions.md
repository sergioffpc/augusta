# CIS Exceptions of the Node's k3s

The Node's k3s, configured by [`config.yaml`](config.yaml) and the files beside
it, is measured against the CIS Kubernetes Benchmark's k3s profile by
`kube-bench` (ADR-0057). This is every control that run does not pass, and why
that is accepted. A `FAIL` not listed here is a defect: fix the configuration,
or add the control here with its reason in the same change.

- **The benchmark.** k3s's own
  [CIS 2.0 self-assessment](https://docs.k3s.io/security/self-assessment-2.0)
  covers its v1.34 to v1.37 releases, the cluster's among them (`K3S_VERSION` in
  [`scripts/bootstrap-node.sh`](../../../scripts/bootstrap-node.sh)).
  `kube-bench` has no profile for it yet. Its newest k3s profile,
  `k3s-cis-1.11`, is the one the runbook runs
  ([Verify the hardening](../../../docs/runbooks/k3s-node-recovery.md#verify-the-hardening)):
  its sections 1, 2 and 4 are the 2.0 guide's controls under the same numbers;
  it adds section 3 (authentication and audit policy) and a 5.2.9 (added
  capabilities), which moves the 2.0 guide's 5.2.9 to 5.2.12 one number on. The
  numbers below are `kube-bench`'s. When `kube-bench` gains a newer k3s profile,
  run that one and update this record.
- **The results.** `kube-bench` reports a control it can check as `PASS` or
  `FAIL`, and a manual one, or an automated one that is not scored, as `WARN`
  when it is not met. A `WARN` is not a failure of the run, but each one is
  listed here too: met by the configuration, or accepted.

## Not applicable to k3s

k3s runs the API server, controller manager, scheduler and kubelet inside its
own process, from flags; `kube-bench` skips these.

| Control                     | Why it does not apply                                                                                                                  |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| 1.1.1 to 1.1.8              | No pod specification files for the control plane or etcd.                                                                              |
| 1.1.12                      | No `etcd` user: the datastore runs as root inside k3s.                                                                                 |
| 4.1.1, 4.1.2, 4.1.9, 4.1.10 | No kubelet service file and no kubelet configuration file: k3s passes the kubelet its flags.                                           |
| 4.2.7                       | k3s sets `--hostname-override` to the Node's name, which the benchmark allows where the node name must match the host's (k3s's guide). |

## Not applicable to a single Node on SQLite

The cluster is one k3s server with its default datastore, SQLite, which the API
server reaches through k3s's kine over a local socket. There is no etcd, no peer
to talk to and no etcd CA. Each control below expects etcd; the reason is the
same for all of them.

| Control    | `kube-bench` reports                                                                                |
| ---------- | --------------------------------------------------------------------------------------------------- |
| 1.1.11     | `WARN`: no etcd data directory.                                                                     |
| 1.2.23     | `PASS`: the check passes when k3s does not manage an etcd cluster.                                  |
| 1.2.26     | `FAIL` if the API server has no `--etcd-cafile`, as it needs none for a local socket: **accepted**. |
| 2.1 to 2.7 | `WARN`: no etcd configuration file to read.                                                         |

Moving to embedded etcd (`cluster-init`) for its snapshots would make these
apply, and pass as k3s's guide shows; it is not done for the benchmark alone.

## Accepted manual controls

| Control         | Why it is left as it is                                                                                                                                                                                                                                                             |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1.2.11          | `AlwaysPullImages` guards images on a node shared by tenants; the Node has one. With it, every container start needs its image's registry, so an outage of one would stop a crashed server from restarting. Image provenance is Kyverno's signature and digest checks (#531, #532). |
| 1.2.20          | The API server's default 60 s request timeout suits the cluster; the benchmark asks to change it only if needed.                                                                                                                                                                    |
| 3.1.1 to 3.1.3  | k3s's kubeconfig authenticates with a client certificate, which cannot be revoked. The operator's day-to-day kubeconfig is a least-privilege one (#528); the k3s admin certificate stays on the Node.                                                                               |
| 5.1.1 to 5.1.13 | RBAC is reviewed by hand, not by `kube-bench`. The cluster's RBAC is k3s's, Flux's, the monitoring stack's and Kyverno's own; the operator's identity is #528.                                                                                                                      |
| 5.2.1 to 5.2.13 | Pod security is each namespace's Pod Security level, set by its labels (#529, #530), and Kyverno (#524, #538); there is no cluster-wide default (`admission.yaml`).                                                                                                                 |
| 5.3.1, 5.3.2    | k3s's network policy controller enforces NetworkPolicies; every namespace's are #536.                                                                                                                                                                                               |
| 5.4.1, 5.4.2    | The cluster holds no Secret its workloads use (ADR-0057, #533).                                                                                                                                                                                                                     |
| 5.5.1           | Image provenance is Kyverno's, not an `ImagePolicyWebhook` (#531, #532).                                                                                                                                                                                                            |
| 5.6.1 to 5.6.4  | Namespaces separate the Environments, Flux, monitoring and Kyverno; the kubelet's `seccomp-default` (below) and each namespace's Pod Security level cover seccomp and security contexts.                                                                                            |

## Manual controls this configuration meets

These report `WARN` on k3s's defaults and pass with [`config.yaml`](config.yaml)
and [`audit-policy.yaml`](audit-policy.yaml):

- 1.2.3 (`DenyServiceExternalIPs`), 1.2.9 (`EventRateLimit`, configured in
  [`admission.yaml`](admission.yaml)).
- 1.2.16 to 1.2.19 (the audit log's path, age, backups and size), 3.2.1 and
  3.2.2 (the audit policy: every request at `Metadata`, Secrets and RBAC objects
  at `RequestResponse`). 3.2.2 stays `WARN` because `kube-bench` cannot judge a
  policy; this one is ADR-0057's.
- 1.2.27, 1.2.28 (secrets encryption at rest, `aescbc`).
- 1.3.1 (`terminated-pod-gc-threshold`).
- 4.2.5 (`streaming-connection-idle-timeout`), 4.2.12 (the kubelet's TLS
  ciphers), 4.2.13 (`pod-max-pids`), 4.2.14 (`seccomp-default`).

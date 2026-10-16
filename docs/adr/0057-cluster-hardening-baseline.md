# Cluster Hardening Baseline

The Node and every workload on it are hardened against four adversaries: a
malicious game client that exploits `augustad` and tries to leave its container,
a compromised device on the LAN, a revision or image that was not ours (supply
chain), and a stolen operator credential. Physical theft of the Node is out of
scope. The Node stays LAN-only for now, but nothing here assumes it: exposing
the game ports to the internet later is one firewall rule, and the control
plane, Grafana and SSH are never exposed.

**The measure is the CIS Kubernetes Benchmark's k3s profile**, checked by
`kube-bench`, with every exception written down, and the **`restricted` Pod
Security Standard** wherever a workload can meet it.

**Admission: Pod Security Admission, then Kyverno, the same policies in CI.**
PSA enforces each namespace's level from inside the API server: `restricted` for
`develop`, `staging`, `flux-system`, `kyverno` and `monitoring`; `privileged`
only for `kube-system` and a `monitoring-node` namespace holding only what reads
the Node itself: node-exporter, the collector of its security logs and
`kube-bench`. Kyverno adds what PSA cannot: `augustad`, Flux and Kyverno images
must carry a valid cosign signature (`augustad`'s, keyless, from this
repository's CI workflow), every other image must be pinned by digest, the
`privileged` namespaces admit only their known pods, and node ports stay in
their Environment's range. Its webhooks fail closed, except for `kube-system`
and `kyverno`, so Kyverno can always start; PSA still holds when it is down. CI
runs the same policies over the rendered manifests (`kyverno apply`), so a pull
request that breaks them fails before it merges.

**Packs and captures are local PersistentVolumes, not `hostPath`.** `baseline`,
let alone `restricted`, forbids a pod's `hostPath` volume. The chart makes, for
each server, a `local` PersistentVolume on the same folder of the Node
(`<environment>/<scenario>/<packVersion>/`, read-only; the capture directory,
read-write) and the claim bound to it, so adding or moving a server is still one
line in its `HelmRelease` (ADR-0026).

**Publishing a pack needs no root.** `augusta-publish` logs in as the Node's
`augusta-publish` user, which owns the pack folder, is not the Servers' user nor
in their group, and has no `sudo`. Its key is restricted in `authorized_keys`
(`restrict`, no shell, PTY or forwarding) to a forced command that only writes a
new `<environment>/<scenario>/<packVersion>/` folder, for a known Environment
alone, and leaves it read-only, so a stolen publish key can add a pack and
nothing else: never replace one a Server runs. Each Environment has its own
tree, its packs signed with its own key, so one cook published for `develop` and
for `staging` is two folders, not one that only the first could write.

**The cluster holds one credential, and Git none.** Grafana has no login at all,
anonymous viewers only, its dashboards, data sources and alerts all from Git;
Loki and Prometheus answer only inside the cluster. The one credential is
Alertmanager's Telegram bot token (ADR-0049), with the chat it posts to, in the
Secret `monitoring/alertmanager-telegram`: the operator makes it by hand on the
Node, and makes it again after a rebuild. A stolen token can post as the bot,
and read what it was sent, but reaches nothing in the cluster. The one Secret in
Git is Flux's trusted signing keys, which are public (ADR-0056). A Kyverno
policy refuses every other Secret but Helm's release records and those the
charts make for themselves, each by name.

**`augustad` cannot reach out, nor in past its container.** Every namespace but
k3s's own denies ingress and egress by default, each flow allowed by name;
`augustad` takes UDP on its game port and Prometheus's scrape, and sends nothing
out. It runs in a user namespace (`hostUsers: false`), so its UID maps to no one
on the Node, under a seccomp profile of the syscalls it makes rather than
`RuntimeDefault`.

**A process that escapes it lands on a hardened kernel.** The Node's kernel
parameters hide kernel addresses (`kptr_restrict`), leave ptrace to root
(`ptrace_scope=2`), refuse to load a kernel without a reboot
(`kexec_load_disabled`), give no user namespace to an unprivileged process
(`unprivileged_userns_clone=0`), blind BPF's JIT, send no ICMP redirect and take
no IPv6 one, and drop a packet whose source no interface could reach back (loose
`rp_filter`, as strict drops flannel's VXLAN). `augustad`'s user namespace is
the container runtime's, made as root, so it still has one; podman on the Node,
which reads a crashed server's image (ADR-0047), runs under `sudo`.

**The Node has no swap.** A server killed for memory restarts, and a Match is
already allowed to be cut (ADR-0026); a Node that swaps slows every Match on it
instead, and holds a copy of memory on disk. The kubelet keeps pods out of swap
anyway, so it served only the Node's own processes.

Flux's NetworkPolicies are ours, not the ones `flux install` generates, which
let its controllers reach anywhere. A Kyverno policy refuses a pod in a
namespace with no default deny, or with a rule that lets everything in or out.
NetworkPolicies name addresses, not hosts: what must reach GitHub, GHCR, the
Helm repositories, Sigstore or Telegram (Flux's source-controller, Kyverno's
image verification, Alertmanager) reaches HTTPS on the internet, never the LAN
or the cluster; the API server, a process of the Node, is reached by its port.

**Security events are searchable, and the important ones alert.** Loki keeps,
beside every pod's log (ADR-0053), the API server's audit log, the Node's logins
and `sudo` (its journal's auth facilities) and auditd's log, Kyverno's policy
violations and Flux's failed signature checks (as the Kubernetes events both
write), and a daily `kube-bench` run's report. The audit log reaches Loki
without request or response bodies, which hold Secrets' contents and stay in the
Node's root-only copy. An SSH login to the Node, a request by k3s's
cluster-admin identity, a policy violation and a source failing verification
each alert in Alertmanager, from Loki's ruler, with ADR-0049's receivers.

## Considered Options

- **PSA alone**: rejected. It cannot verify an image's signature or confine a
  `privileged` namespace to known pods.
- **sigstore's policy-controller in place of Kyverno**: rejected. It only
  verifies signatures; the other rules would need a second engine.
- **A `hostPath` exception per namespace, kept in Kyverno**: rejected. The
  namespace's PSA level would have to drop to `privileged`, leaving the server
  namespaces as weak as their weakest exception whenever Kyverno is down.
- **Packs as OCI images, mounted as `image` volumes and signed with cosign**:
  deferred. It removes the Node's pack folders altogether, but reworks
  publishing (ADR-0026), and is a project of its own.
- **Secrets encrypted in Git (SOPS and age)**: rejected while one credential is
  all the cluster holds. It brings a key to keep, rotate and give the cluster,
  itself a Secret made by hand, to save making the one Secret it protects by
  hand.
- **Runtime detection (Falco, Tetragon)**: deferred until a game port faces the
  internet.
- **Swap kept, with `vm.swappiness` low**: rejected. It still lets a Node that
  runs out of memory slow down rather than kill, for no process that needs it.

## Consequences

- A workload that needs more than `restricted` needs a policy change that a
  reviewer sees, not just a manifest.
- A reboot that brings Kyverno up late holds every new pod but k3s's and its own
  back until it is ready; the pods the Node ran start again without admission.
- An upstream chart upgrade can fail admission until its images' digests are
  updated with it.
- The node-exporter moves out of `monitoring` into `monitoring-node`, where the
  security logs' collector and `kube-bench` join it, both reading the Node's
  root-only files as root.
- Loki, which anything in the cluster can read, holds who did what to the
  cluster and the Node, though no Secret's contents.
- kexec stays refused until the Node reboots, even with its parameter removed,
  so a crash kernel (kdump) needs both. A rootless container on the Node does
  not start.
- Kyverno admits any Sigstore bundle the identity made for the image's digest,
  so CI's own provenance and SBOM attestations satisfy the policy as its cosign
  signature does: admission proves an image came from this repository's CI
  workflow, not that a signature proper exists. CI's publish job proves that,
  checking the verified bundles for a cosign signature (`jq` on `critical.type`)
  and failing without one.

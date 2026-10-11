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
only for `kube-system` and a `monitoring-node` namespace holding node-exporter
alone. Kyverno adds what PSA cannot: `augustad`, Flux and Kyverno images must
carry a valid cosign signature (`augustad`'s, keyless, from this repository's CI
workflow), every other image must be pinned by digest, the `privileged`
namespaces admit only their known pods, and node ports stay in their
Environment's range. Its webhooks fail closed, except for `kube-system` and
`kyverno`, so Kyverno can always start; PSA still holds when it is down. CI runs
the same policies over the rendered manifests (`kyverno apply`), so a pull
request that breaks them fails before it merges.

**Packs and captures are local PersistentVolumes, not `hostPath`.** `baseline`,
let alone `restricted`, forbids a pod's `hostPath` volume. The chart makes, for
each server, a `local` PersistentVolume on the same folder of the Node
(`<scenario>/<packVersion>/`, read-only; the capture directory, read-write) and
the claim bound to it, so adding or moving a server is still one line in its
`HelmRelease` (ADR-0026).

**The cluster holds no Secret.** Nothing in it needs one: Grafana has no login
at all, anonymous viewers only, its dashboards, data sources and alerts all from
Git; Loki and Prometheus answer only inside the cluster. The one Secret in Git
is Flux's trusted signing keys, which are public (ADR-0056).

**`augustad` cannot reach out, nor in past its container.** Every namespace but
k3s's own denies ingress and egress by default, each flow allowed by name;
`augustad` takes UDP on its game port and Prometheus's scrape, and sends nothing
out. Its chart carries its own NetworkPolicy, the cluster's infrastructure
everyone else's, and Flux's are ours rather than the ones `flux install`
generates, which let its controllers reach anywhere. A Kyverno policy refuses a
pod in a namespace with no default deny, or with a rule that lets everything in
or out. NetworkPolicies name addresses, not hosts: what must reach GitHub, GHCR,
the Helm repositories or Sigstore (Flux's source-controller, Kyverno's image
verification) reaches HTTPS on the internet, never the LAN or the cluster, and
the API server, a process of the Node, is reached by its port. It runs in a user
namespace (`hostUsers: false`), so its UID maps to no one on the Node, under a
seccomp profile of the syscalls it makes rather than `RuntimeDefault`.

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
- **Secrets encrypted in Git (SOPS and age)**: rejected while nothing needs a
  Secret. It brings a key to keep and rotate, for no credential the cluster
  uses.
- **Runtime detection (Falco, Tetragon)**: deferred until a game port faces the
  internet.

## Consequences

- A workload that needs more than `restricted` needs a policy change that a
  reviewer sees, not just a manifest.
- A reboot that brings Kyverno up late holds every other pod back until it is
  ready.
- An upstream chart upgrade can fail admission until its images' digests are
  updated with it.
- The node-exporter moves out of `monitoring` into `monitoring-node`.

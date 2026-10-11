# Recover or Rebuild the k3s Node

Brings the single-node k3s cluster that runs the `develop` and `staging`
environments back up, either by recovering the running install or by rebuilding
it from the repository. The cluster's design is ADR-0026
([CD strategy](../adr/0026-cd-strategy.md)) and
[ENGINEERING.md's Deployment & CD](../ENGINEERING.md#deployment--cd).

## When to use

- The node is up but `kubectl` cannot reach the cluster, the node is `NotReady`,
  or Flux's controllers are not running: **Recover** (part A).
- k3s will not start after part A, its datastore is corrupt, or the node's OS or
  disk was replaced: **Rebuild** (part B).
- An environment's `HelmRelease` sets `captures.hostPath` and its pod waits in
  `ContainerCreating`, its events naming `/srv/augusta/captures`: run
  `scripts/bootstrap-node.sh` on the node, which sets up
  [The capture filesystem](#the-capture-filesystem) and skips what is there. A
  change that turns captures on needs it before it merges.
- A change to k3s's configuration (`clusters/onprem/node/`) or release
  (`K3S_VERSION`) has merged, or the node's CIS hardening is to be checked:
  [The k3s configuration](#the-k3s-configuration).
- A change to the node's firewall, SSH, update or audit policy has merged, the
  maintainer's addresses change, or a game port range is to face the internet:
  [The node's OS](#the-nodes-os).

## What the cluster holds

Everything Flux deploys is in Git: Flux's own manifests in
[`clusters/onprem/flux-system/`](../../clusters/onprem/flux-system), the
monitoring stack's `HelmRelease` in
[`clusters/onprem/infrastructure/`](../../clusters/onprem/infrastructure), the
two augustad `HelmRelease` objects and their GitRepository sources in
[`clusters/onprem/apps/`](../../clusters/onprem/apps), and the chart in
[`charts/augustad/`](../../charts/augustad). The `flux-system` GitRepository
reads the public repository over HTTPS with no credentials, so a rebuilt cluster
needs no deploy key.

The only state not in Git is the asset packs on the node's shared volume:
`/srv/augusta/asset-packs/<scenario>/<packVersion>/server.pack` and
`signing.pub` for each server the `HelmRelease` values in
`clusters/onprem/apps/` list
([`charts/augustad/values.yaml`](../../charts/augustad/values.yaml)). Without
them the servers never start; the layout, and how a missing folder shows, is
[Where packs go on the node](pack-key-rotation.md#where-packs-go-on-the-node).

The Match captures (ADR-0050) of every environment that turns them on (its
`HelmRelease` sets `captures.hostPath`; `develop` does) are on the node too, in
`/srv/augusta/captures/<namespace>/<scenario>/`, on a filesystem of their own
([The capture filesystem](#the-capture-filesystem)). They are debugging data: a
rebuild may keep or drop them, but the filesystem must be set up again before
Flux starts those servers.

Prometheus keeps its last 15 days of series on a `local-path` volume on the node
(ADR-0049). A rebuild loses them, and Prometheus starts again empty; no step
restores them.

## Prerequisites

- SSH access to the node (`<node>` below), by key, from one of the maintainer's
  addresses, as a user with `sudo`, not root ([SSH](#ssh)); or its console. The
  repository does not record its address or OS.
- A Debian 13 node.
  [`scripts/bootstrap-node.sh`](../../scripts/bootstrap-node.sh) (README.md,
  Bootstrap), which part B runs, sets it up: the server's user, the asset-pack
  volume, the capture filesystem, the crash-core handler (ADR-0047), k3s at the
  release it pins (`K3S_VERSION`, whose Kubernetes version is
  `KUBERNETES_VERSION` in the `manifests` job of
  [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)) with
  [its configuration](#the-k3s-configuration),
  [the OS's hardening](#the-nodes-os), and Flux. It skips what the node already
  has.
- For a rebuild, a copy of every environment's pack folder: either taken from
  the node in step B1, or re-made from the pack environment that cooked them
  (`<AssetsRoot>\packs\<scenario>\server.pack` and the public key it was signed
  with, see [Rotate the Pack Signing Key](pack-key-rotation.md)). The repository
  defines no backup of the volume.
- The server image, `ghcr.io/sergioffpc/augustad`, pullable without credentials:
  the chart sets no `imagePullSecrets`, so the GHCR package must be public.
- On the workstation: `kubectl`, the `flux` CLI
  ([as for a rollback](flux-rollback.md#prerequisites)) and `openssl`, the
  day-to-day kubeconfig ([Cluster access](#cluster-access)), and this repository
  with `origin/develop` fetched: the `flux-system` GitRepository tracks
  `develop`.

## The server's user

The server image runs as `augusta`, UID and GID 65532 (ADR-0054). The node has
the same user and group, with no login and no home, so its files show that name
and the node's own users stay out of them:

- **Asset packs:** `/srv/augusta/asset-packs` and everything in it is owned by
  `root:augusta`, folders `0750` and files `0640`. The servers read it as group
  `augusta`; only root writes it (`augusta-publish` installs packs so).
- **Captures:** the mounted capture filesystem's root is owned by
  `augusta:augusta` ([The capture filesystem](#the-capture-filesystem)).

`scripts/bootstrap-node.sh` creates the user and an empty volume. Packs already
on a node from before this layout are brought to it with:

```sh
ssh <node>
sudo chown -R root:augusta /srv/augusta/asset-packs
sudo find /srv/augusta/asset-packs -type d -exec chmod 0750 {} +
sudo find /srv/augusta/asset-packs -type f -exec chmod 0640 {} +
sudo ls -lR /srv/augusta/asset-packs
```

## The capture filesystem

Servers whose environment sets `captures.hostPath` mount `/srv/augusta/captures`
read-write, and each writes its captures into its own `<namespace>/<scenario>/`
folder there: a capture's name (when its Match started and its number in the
server's run) is unique only within one server. augustad creates that folder
itself, and refuses to start if it cannot.

- **Path and owner:** `/srv/augusta/captures`, owned by `augusta:augusta`, mode
  `0755`: [the server's user](#the-servers-user). The chart's `hostPath` volume
  is of type `Directory`, so the kubelet never creates it, as `root:root`, in
  its place; a pod whose directory is missing waits in `ContainerCreating`.
- **Filesystem:** an ext4 image of fixed size, `/srv/augusta/captures.img`,
  readable by root only and loop-mounted on that path. Its blocks are allocated
  when it is made, so captures never take more of the node's disk than that and
  never cause DiskPressure. A full filesystem fails a capture's next write:
  augustad stops that Match's capture, logged once
  (`subsystem=capture event=capture_stopped reason=write_failed`), and the
  server and its Match go on.
- **Size:** 4 GiB. It must hold, at once, every capturing server's retention cap
  (#461: each server deletes its own oldest captures when a Match starts, until
  they fit its `captures.retention.maxMiB`, the Match in progress included),
  plus slack for the filesystem's own overhead:
  `size >= capturing servers * maxMiB + 256 MiB`. `develop`'s one server
  (`firebase`) at a `maxMiB` of up to 3840 fits; at about 15 KB a second with 8
  players at 60 Hz, 1 GiB is about 19 hours of Matches. A new capturing server,
  or a higher `maxMiB`, grows the image first (below). Until #461 lands nothing
  deletes captures, and a full filesystem only stops them.

`scripts/bootstrap-node.sh` sets it up, with its `/etc/fstab` line, before it
installs k3s. To check it:

```sh
ssh <node> "findmnt /srv/augusta/captures && ls -ld /srv/augusta/captures"
```

The mount point itself stays `root:root`; only the mounted filesystem's root is
`augusta`'s. If the image is ever not mounted, augustad cannot create its folder
in the bare directory and refuses to start, rather than write captures to the
node's own disk. A server started before the mount sees the bare directory until
its pod is restarted (**cluster-admin**, on the node:
`sudo k3s kubectl -n <namespace> rollout restart deploy/<name>`).

To grow it, stop every capturing server first: a running pod keeps the
filesystem in use. From the workstation, for each capturing environment:

```sh
flux suspend helmrelease augustad-<environment> -n flux-system
```

Then on the node, scaling each of them to zero (**cluster-admin**) before
growing the image:

```sh
sudo k3s kubectl -n <environment> scale deploy -l app.kubernetes.io/name=augustad --replicas 0
sudo umount /srv/augusta/captures
sudo fallocate -l <new-size>GiB /srv/augusta/captures.img
sudo e2fsck -f /srv/augusta/captures.img
sudo resize2fs /srv/augusta/captures.img
sudo mount /srv/augusta/captures
sudo k3s kubectl -n <environment> scale deploy -l app.kubernetes.io/name=augustad --replicas 1
```

Then from the workstation, for each environment stopped above:

```sh
flux resume helmrelease augustad-<environment> -n flux-system
```

Record the new size in this section.

## The node's OS

Debian 13 is hardened against a compromised device on the LAN and a stolen
credential (ADR-0057). Its configuration is in
[`clusters/onprem/node/`](../../clusters/onprem/node), beside k3s's, and
`scripts/bootstrap-node.sh` writes it on every run, before k3s's:

| In the repository              | On the node                                                  | What it sets                                                                   |
| ------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------------------------ |
| `nftables.conf`                | `/etc/nftables.conf`                                         | [The firewall](#the-firewall).                                                 |
| `nftables.service.conf`        | `/etc/systemd/system/nftables.service.d/augusta.conf`        | Stopping the firewall removes its table alone, not k3s's.                      |
| `sshd.conf`                    | `/etc/ssh/sshd_config.d/10-augusta.conf`                     | [SSH](#ssh): keys only, no root login.                                         |
| `unattended-upgrades.conf`     | `/etc/apt/apt.conf.d/52augusta-upgrades`                     | [Updates](#updates): Debian's security updates, a reboot at 05:00 when needed. |
| `apt-daily-upgrade.timer.conf` | `/etc/systemd/system/apt-daily-upgrade.timer.d/augusta.conf` | Updates install between 04:00 and 04:30.                                       |
| `audit.rules`                  | `/etc/audit/rules.d/augusta.rules`                           | [The audit trail](#the-audit-trail).                                           |

To change any of it, merge the change to `develop`, then run the bootstrap on
the node, which writes what changed and reloads the service that reads it:

```sh
ssh <node> "curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash"
```

The first run on a node installed before this section existed is the same
command, once your key is in place ([SSH](#ssh)); the node takes the address you
run it from as the maintainer's ([The firewall](#the-firewall)). On a node whose
k3s is not hardened yet, the run then stops at
[Harden a running node](#harden-a-running-node), with the OS's part written.
Then [Verification](#verification)'s port scan and refused logins.

### The firewall

nftables denies every inbound connection that is not listed:

| Port                  | From                                                          | For                                        |
| --------------------- | ------------------------------------------------------------- | ------------------------------------------ |
| 22/TCP, 6443/TCP      | The maintainer's addresses                                    | SSH, and `kubectl` and `flux`.             |
| 30700-30899/UDP       | The LAN (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, fc00::/7) | The game ports of `develop` and `staging`. |
| 30300/TCP             | The LAN                                                       | Grafana.                                   |
| 6443, 9100, 10250/TCP | The node's own pods (10.42.0.0/16, on `cni0`)                 | Prometheus's scrapes.                      |
| Anything              | The node itself                                               | The kubelet, flannel, k3s's own ports.     |

The pod and service networks (10.42.0.0/16, 10.43.0.0/16) take traffic from the
node's pods alone: a LAN host that routes them through the node reaches nothing.
The LAN is the private ranges: a client on a global IPv6 address counts as the
internet, so LAN players join over IPv4. Ping, path MTU and address
configuration (DHCP, IPv6 neighbour discovery) are let in too. Outbound is not
filtered. The table, `inet augusta`, filters before kube-proxy redirects a node
port to its pod, and lives beside k3s's own tables: never `nft flush ruleset` on
the node, which deletes those too. To see it and what it holds:

```sh
sudo nft list table inet augusta
```

**The maintainer's addresses** are the node's, not the repository's:
`/etc/nftables.d/admin-addresses.nft`, which the bootstrap writes. A new node
takes the address of the SSH session that runs it, so run it as yourself, not
under `sudo`, which hides the session from it. To set them, pass each address or
range, the one you are connecting from included:

```sh
ssh <node> "curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash -s -- --admin <address> [--admin <address>...]"
```

The list given replaces the node's. If it leaves out the session's own address,
the bootstrap takes the firewall down again and stops, rather than leave you
unable to reconnect. Locked out all the same (a workstation's address changed),
fix the list from the node's console and reload:
`sudo systemctl reload nftables`.

**Facing the internet** is one rule and one router forward, never for SSH, the
API server or Grafana. In `nftables.conf`, before the final `drop`, add the
range with no source, for example `staging`'s:

```text
udp dport 30800-30899 accept
```

merge it, run the bootstrap, and forward that UDP range on the router to the
node.

### SSH

sshd takes keys only: no password, no keyboard-interactive prompt, no root
login, whatever another drop-in says. Log in as your own user and `sudo`.

Before the bootstrap first writes this, your key must be in that user's
`~/.ssh/authorized_keys` on the node: it stops when the user running it is root
or has no key there. Make the key hardware-backed where you have a FIDO2
security key, so that a copy of the key file is useless without it and every
login needs a touch:

```sh
ssh-keygen -t ed25519-sk -C "<you>@augusta-node"
ssh-copy-id -i ~/.ssh/id_ed25519_sk.pub <node>
```

Without one, an `ed25519` key with a passphrase (`ssh-keygen -t ed25519`, then a
passphrase at its prompt). Remove any other key from `authorized_keys` once the
new one works.

### Updates

`unattended-upgrades` installs Debian's security updates every night between
04:00 and 04:30, and nothing else. When one needs a reboot (a kernel, libc), the
node reboots at 05:00, whoever is logged in, cutting any Match in progress
(ADR-0026 accepts a cut Match); k3s, the servers and the capture filesystem
start again with it. k3s is not a Debian package: it changes only by
[Upgrade k3s](#upgrade-k3s).

```sh
ssh <node>
sudo unattended-upgrade --dry-run --debug 2>&1 | grep 'Allowed origins'
sudo tail /var/log/unattended-upgrades/unattended-upgrades.log
cat /var/run/reboot-required 2>/dev/null
```

The first prints only `Debian-Security`; the last says whether a reboot waits
for 05:00.

### The audit trail

auditd records every login, by SSH or on the console (PAM's `USER_START`, with
the account and the address), and a change to the files below, each under a key:

| Key            | What changed                                        |
| -------------- | --------------------------------------------------- |
| `k3s-config`   | `/etc/rancher/k3s/`, the k3s binary or its service. |
| `sshd`         | sshd's configuration.                               |
| `privilege`    | `sudoers`, and every `sudo` or `su` by a person.    |
| `identity`     | `passwd`, `group`, `shadow`, `gshadow`.             |
| `firewall`     | `/etc/nftables.conf`, `/etc/nftables.d/`.           |
| `updates`      | `/etc/apt/apt.conf.d/`.                             |
| `audit-config` | `/etc/audit/`.                                      |

```sh
ssh <node>
sudo ausearch -i -m USER_START -ts today | grep sshd
sudo ausearch -i -k k3s-config -ts today
```

The log is `/var/log/audit/audit.log`, rotated by auditd.

## Cluster access

Two identities reach the API server, from the maintainer's addresses only
([The firewall](#the-firewall)):

- **Day to day: `augusta:operators`.** A client certificate that expires a day
  after it is issued, in `~/.kube/augusta.yaml` on the workstation. It reads
  everything but Secrets, and suspends, resumes and reconciles every Flux
  source, Kustomization and `HelmRelease`. It cannot create, change or delete a
  workload, a Secret, a Role or ClusterRole, or a binding, nor change a Flux
  object beyond `spec.suspend` and its `reconcile.fluxcd.io/` annotations (a
  source's URL or a release's values would deploy anything). Its role and that
  limit are in
  [`clusters/onprem/infrastructure/operator.yaml`](../../clusters/onprem/infrastructure/operator.yaml).
- **cluster-admin.** k3s's own kubeconfig, `/etc/rancher/k3s/k3s.yaml`, which
  stays on the node: never keep a copy of it elsewhere. Only the steps marked
  **cluster-admin** in these runbooks need it, and every `sudo k3s kubectl` run
  on the node is one.

### Issue or renew the day-to-day kubeconfig

On the workstation, from this repository, with `openssl` and SSH to the node as
a user with `sudo`:

```sh
scripts/operator-kubeconfig.sh <node>
export KUBECONFIG=~/.kube/augusta.yaml
kubectl get nodes
```

It makes a new key, which stays on the workstation, and has the node sign its
certificate for 24 hours, then prints when it expires. Run it again whenever
`kubectl` answers `the server has asked for the client to provide credentials`:
an expired certificate is the usual cause. The certificate names you by your
workstation user, which the API server's audit log records.

To check what it may do:

```sh
kubectl auth can-i --list
kubectl get pods -A
flux get all -A
flux suspend kustomization apps -n flux-system
flux resume kustomization apps -n flux-system
kubectl -n develop create deployment probe --image=busybox
kubectl create clusterrolebinding probe --clusterrole=cluster-admin --user=probe
kubectl -n flux-system patch gitrepository flux-system --type merge -p '{"spec":{"interval":"2m"}}'
```

The reads and the Flux commands work; the last three are refused, the patch by
the `augusta-operator-flux` policy.

### Use cluster-admin

A **cluster-admin** step that runs on the node uses `sudo k3s kubectl` there.
One that needs a tool on the workstation (`helm`, a `kubectl` across many steps)
takes k3s's kubeconfig for that shell alone, and removes it when done:

```sh
export KUBECONFIG="$(mktemp)"
ssh <node> "sudo cat /etc/rancher/k3s/k3s.yaml" \
  | sed 's#https://127.0.0.1:6443#https://<node-address>:6443#' > "${KUBECONFIG}"
# ... the cluster-admin step ...
rm "${KUBECONFIG}"
export KUBECONFIG=~/.kube/augusta.yaml
```

A copy of k3s's kubeconfig kept from before (the old Part B step 4 wrote
`~/.kube/augusta.yaml` from it) is cluster-admin until its certificate expires,
about a year after k3s issued it: replace it by issuing the day-to-day one
above. Revoking it sooner means rotating k3s's client CA
([`k3s certificate rotate-ca`](https://docs.k3s.io/cli/certificate#certificate-authority-ca-certificates)),
which also ends every node and controller certificate signed by it.

## The k3s configuration

k3s runs hardened from its first start, to the CIS Kubernetes Benchmark's k3s
profile (ADR-0057). Its configuration is in
[`clusters/onprem/node/`](../../clusters/onprem/node), and
`scripts/bootstrap-node.sh` writes it on every run, before it installs k3s:

| In the repository   | On the node                          | What it sets                                                                                                    |
| ------------------- | ------------------------------------ | --------------------------------------------------------------------------------------------------------------- |
| `config.yaml`       | `/etc/rancher/k3s/config.yaml`       | k3s's flags: secrets encryption, the audit log, admission plugins, the kubelet's hardening.                     |
| `audit-policy.yaml` | `/etc/rancher/k3s/audit-policy.yaml` | What the API server logs: every request at `Metadata`, Secrets and RBAC objects at `RequestResponse`.           |
| `admission.yaml`    | `/etc/rancher/k3s/admission.yaml`    | `EventRateLimit`'s limits. No Pod Security default: each namespace's labels set its level.                      |
| `90-kubelet.conf`   | `/etc/sysctl.d/90-kubelet.conf`      | The kernel parameters `protect-kernel-defaults` requires; without them the kubelet, and so k3s, does not start. |

- **Secrets encryption:** k3s encrypts Secrets in its datastore with a key it
  generates on first start, in `/var/lib/rancher/k3s/server/cred/`. A rebuild
  makes a new key along with the new datastore.
- **Audit log:** `/var/lib/rancher/k3s/server/logs/audit.log`, rotated at 100
  MB, with 10 old files kept for up to 30 days. At `RequestResponse`, a read or
  write of a Secret logs its contents, so the directory is root's only (`0700`)
  and so are its files; do not copy the log off the node.
- **Exceptions:** every CIS control the node does not pass, and why, is in
  [`cis-exceptions.md`](../../clusters/onprem/node/cis-exceptions.md).

### Change k3s's configuration

A change to `clusters/onprem/node/` merges to `develop` like any other. Then, on
the node, run the bootstrap to write it, and restart k3s to apply it:

```sh
ssh <node> "curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash"
ssh <node> "sudo systemctl restart k3s && sudo k3s kubectl wait --for=condition=Ready node --all --timeout=5m"
```

The bootstrap says when the configuration it wrote differs from what was there.
A restart stops the servers' Matches for the minute k3s takes to come back. A
k3s that does not come back is [Part A](#part-a-recover)'s: its journal names
the flag or kernel parameter it rejects. To back out, revert the change and
repeat. Then [Verify the hardening](#verify-the-hardening).

### Harden a running node

Once, on a node installed before its configuration was in the repository. The
configuration applies as a change does (above), except for secrets encryption:
the existing Secrets are encrypted in steps, as k3s's
[Enable Secrets Encryption on an Existing Cluster](https://docs.k3s.io/cli/secrets-encrypt#enable-secrets-encryption-on-an-existing-cluster)
describes. Until step 1 has prepared encryption, the bootstrap refuses to run on
that node: it would write the configuration, and k3s would take it unprepared at
its next restart.

1. Check that encryption is off, and prepare it while k3s still runs without it:

    ```sh
    ssh <node>
    sudo k3s secrets-encrypt status
    sudo k3s secrets-encrypt enable
    ```

    The status shows `Disabled, no configuration file found`.

2. Write the configuration, and restart k3s with it:

    ```sh
    curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash
    sudo systemctl restart k3s
    sudo k3s secrets-encrypt status
    ```

    The status shows `Disabled`, rotation stage `start`, and `All hashes match`.

3. Rotate the keys, which turns encryption on and re-encrypts every Secret, then
   restart k3s again:

    ```sh
    sudo k3s secrets-encrypt rotate-keys
    sudo k3s secrets-encrypt status
    sudo systemctl restart k3s
    sudo k3s secrets-encrypt status
    ```

    Restart only once the stage is `reencrypt_finished`; k3s re-encrypts about 5
    Secrets a second. The last status shows `Enabled`.

4. [Verify the hardening](#verify-the-hardening).

If k3s does not start after step 2, its journal names the cause: a kernel
parameter (`sudo sysctl --system` loads `90-kubelet.conf`) or a rejected flag.
Nothing is encrypted yet then, so to abort, remove
`/etc/rancher/k3s/config.yaml` and restart k3s, which runs on its defaults
again. From step 3 on, `secrets-encryption` stays in the configuration: without
it the API server cannot read the Secrets it encrypted. Turning it off again is
k3s's
[disable procedure](https://docs.k3s.io/cli/secrets-encrypt#secrets-encryption-disablere-enable).

### Upgrade k3s

k3s is upgraded by hand, to the release the repository pins, one minor version
at a time; nothing on the node upgrades it unattended.

1. In a pull request to `develop`, set `K3S_VERSION` in
   `scripts/bootstrap-node.sh` to the new release, and `KUBERNETES_VERSION` in
   the `manifests` job of `.github/workflows/ci.yml` to its Kubernetes version.
   The job fails when they differ, and validates the manifests against the new
   version. Read the release's notes for removed APIs and flags, and k3s's
   [CIS hardening guide](https://docs.k3s.io/security/hardening-guide) for it,
   and change `clusters/onprem/node/` with them if needed.
2. Once it has merged, write any configuration it changed and install the new
   release over the running one; the install script keeps `/etc/rancher/k3s/`
   and the datastore:

    ```sh
    ssh <node>
    curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash
    curl -sfL https://get.k3s.io | sudo INSTALL_K3S_VERSION=<K3S_VERSION> sh -
    k3s --version
    sudo k3s kubectl wait --for=condition=Ready node --all --timeout=5m
    ```

    The bootstrap reports the installed release it leaves; the install then
    replaces it with `<K3S_VERSION>`, the pull request's.

3. [Verify the hardening](#verify-the-hardening), then the
   [Verification](#verification) below.

A release that misbehaves within its minor version is rolled back the same way,
with the previous release. Going back a minor version on the same datastore is
not supported: that needs Part B, at the previous release.

### Verify the hardening

On the node, after a rebuild, a configuration change or an upgrade:

1. Run `kube-bench` with the k3s profile. Its latest release has k3s profiles
   only up to CIS 1.9, so it runs with the `k3s-cis-1.11` profile from the
   `kube-bench` commit that added it
   ([`cis-exceptions.md`](../../clusters/onprem/node/cis-exceptions.md) says why
   that profile):

    ```sh
    ssh <node>
    mkdir -p ~/kube-bench && cd ~/kube-bench
    release=https://github.com/aquasecurity/kube-bench/releases/download/v0.16.0
    arch="$(dpkg --print-architecture)"
    curl -fsSLO "${release}/kube-bench_0.16.0_linux_${arch}.tar.gz"
    curl -fsSLO "${release}/kube-bench_0.16.0_checksums.txt"
    sha256sum --check --ignore-missing kube-bench_0.16.0_checksums.txt
    tar -xzf "kube-bench_0.16.0_linux_${arch}.tar.gz" kube-bench
    rm -rf cfg
    curl -fsSL https://codeload.github.com/aquasecurity/kube-bench/tar.gz/8102dfb87b02d708356b8560a04a6d7f9b38965b \
      | tar -xz --strip-components=1 --wildcards 'kube-bench-*/cfg'
    sudo KUBECONFIG=/etc/rancher/k3s/k3s.yaml ./kube-bench run \
      --config-dir cfg --benchmark k3s-cis-1.11 | tee kube-bench.txt
    grep -E '^\[(FAIL|WARN)\]' kube-bench.txt
    ```

    Every `FAIL` and every `WARN` is in
    [`cis-exceptions.md`](../../clusters/onprem/node/cis-exceptions.md), as
    accepted or as met by the configuration. Fix the configuration for any
    other, or record it there with its reason.

2. Secrets are encrypted at rest:

    ```sh
    sudo k3s secrets-encrypt status
    ```

    It shows `Encryption Status: Enabled` and `All hashes match`.

3. The audit log is written, at both levels. Write a Secret and remove it, then
   count the levels of its entries and of the whole log:

    ```sh
    log=/var/lib/rancher/k3s/server/logs/audit.log
    sudo ls -la "$(dirname "${log}")"
    sudo k3s kubectl -n default create secret generic audit-probe --from-literal=probe=1
    sudo k3s kubectl -n default delete secret audit-probe
    sudo grep '"name":"audit-probe"' "${log}" | grep -o '"level":"[A-Za-z]*"' | sort | uniq -c
    sudo grep -o '"level":"[A-Za-z]*"' "${log}" | sort | uniq -c
    ```

    The directory is `drwx------ root root` and `audit.log` is readable by root
    alone; the probe's entries are all `RequestResponse`; the whole log has
    `Metadata` entries too.

4. The kubelet refuses an anonymous request:

    ```sh
    curl -sk -o /dev/null -w '%{http_code}\n' https://127.0.0.1:10250/pods
    ```

    It prints `401`.

5. From the workstation, both environments and the monitoring stack run as they
   did: [Verification](#verification).

## Part A: Recover

1. Check the k3s service and its log on the node:

    ```sh
    ssh <node>
    sudo systemctl status k3s
    sudo journalctl -u k3s --since "1 hour ago" --no-pager | tail -n 200
    df -h / /var/lib/rancher /srv/augusta /srv/augusta/captures
    ```

    A full disk (DiskPressure, evicted pods, a datastore that cannot write) is
    fixed by freeing space before anything else, for example
    `sudo k3s crictl rmi --prune` for unused images. A full
    `/srv/augusta/captures` needs no freeing: it stops captures, not servers.

2. Restart k3s and wait for the node:

    ```sh
    sudo systemctl restart k3s
    sudo k3s kubectl get nodes
    sudo k3s kubectl get pods -A
    ```

3. From the workstation, check Flux and have it reconcile everything:

    ```sh
    flux check
    flux get all -n flux-system
    flux reconcile kustomization flux-system -n flux-system --with-source
    flux reconcile kustomization apps -n flux-system --with-source
    ```

4. If the node is `Ready` but an environment is not, continue with
   [Roll Back a Bad Deploy with Flux](flux-rollback.md). If k3s does not stay
   up, go to part B.

## Part B: Rebuild

1. Copy the asset packs off the node, if its disk is still readable:

    ```sh
    ssh <node> "sudo tar -C /srv/augusta -czf /tmp/asset-packs.tgz asset-packs"
    scp <node>:/tmp/asset-packs.tgz .
    ```

2. Remove the old install. The script ships with an install made by k3s's
   install script; it removes `/etc/rancher/k3s` (k3s's configuration, which
   step 3 writes again) and `/var/lib/rancher/k3s` (the datastore, its
   encryption key and the audit log), not `/srv/augusta`:

    ```sh
    ssh <node> "sudo /usr/local/bin/k3s-uninstall.sh"
    ```

    On a reinstalled OS there is nothing to remove; skip this step.

3. Run the node's bootstrap, giving it the packs copied in step 1. It sets up
   the server's user, the pack volume (unpacking the archive into it, owners and
   modes kept), the capture filesystem (whose image and `/etc/fstab` line k3s's
   uninstall script leaves, and a reinstalled OS does not), k3s with
   [its configuration](#the-k3s-configuration), and Flux from `develop`'s own
   manifests:

    ```sh
    scp asset-packs.tgz <node>:/tmp/
    ssh <node> "curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash -s -- --packs /tmp/asset-packs.tgz [--admin <address>...]"
    ssh <node> "rm /tmp/asset-packs.tgz; sudo ls -lR /srv/augusta/asset-packs"
    ```

    On a reinstalled OS, log in as your own user, with your key already in its
    `authorized_keys`: from this run on SSH takes keys only, not root
    ([SSH](#ssh)), and only from the maintainer's addresses, which are this
    session's unless `--admin` lists them ([The firewall](#the-firewall)). A
    node that kept its OS keeps its list.

    From there Flux manages itself (the `flux-system` Kustomization applies
    `clusters/onprem/flux-system/`), then installs the monitoring stack and both
    environments.

    Without a copy, run it without `--packs`, then publish each `packVersion`
    that `git grep -n packVersion -- clusters/` lists again, as step 6 of
    [Rotate the Pack Signing Key](pack-key-rotation.md) does, from the packs of
    the cook it names. Until then each server waits in `ContainerCreating`. A
    pack is found by its hash: only the server pack whose BLAKE3 hash starts
    with that version publishes to it, so a lost cook cannot be replaced by a
    new one under the same version. Cook, publish and point the server at the
    new version instead.

4. Issue the day-to-day kubeconfig ([Cluster access](#cluster-access)); the new
   cluster has a new CA, so the old one no longer verifies:

    ```sh
    scripts/operator-kubeconfig.sh <node>
    export KUBECONFIG=~/.kube/augusta.yaml
    kubectl get nodes
    ```

    It reads nothing until Flux has applied its role with the `infrastructure`
    Kustomization, a few minutes after step 3: until then `kubectl` answers
    `Forbidden`.

5. Watch it converge:

    ```sh
    flux get kustomizations -n flux-system --watch
    flux get helmreleases -n flux-system
    ```

## Verification

```sh
flux check
flux get all -n flux-system
kubectl get nodes
kubectl -n develop rollout status deploy -l app.kubernetes.io/name=augustad --timeout 30m
kubectl -n staging rollout status deploy -l app.kubernetes.io/name=augustad --timeout 30m
kubectl -n develop logs -l app.kubernetes.io/name=augustad --prefix | grep 'event=pack_verified'
kubectl -n staging logs -l app.kubernetes.io/name=augustad --prefix | grep 'event=pack_verified'
kubectl -n develop logs -l app.kubernetes.io/name=augustad --prefix | grep 'event=capture_enabled'
ssh <node> "findmnt /srv/augusta/captures && sudo ls -lR /srv/augusta/captures"
kubectl get svc -A -l app.kubernetes.io/name=augustad
kubectl -n monitoring get pods
kubectl -n monitoring-node get pods
kubectl get namespaces -L pod-security.kubernetes.io/enforce
```

- The node is `Ready`; every Flux source, Kustomization and `HelmRelease` is
  `Ready`.
- Every server logged `event=pack_verified` and keeps running.
- Every `develop` server logged `event=capture_enabled` with its own
  `/srv/augusta/captures/develop/<scenario>` directory; the capture filesystem
  is mounted, and a Match played to its end leaves a `.capture` file there.
- Each server's Service is on the node port its `HelmRelease` pins, in its
  environment's range: `develop`'s 30700-30799 (`firebase` on 30700),
  `staging`'s 30800-30899 (`firebase` on 30800).
- node-exporter, alone in `monitoring-node`, is `Running`. `flux-system`,
  `kyverno` and `monitoring` enforce the `restricted` Pod Security level;
  `kube-system` and `monitoring-node`, `privileged` (ADR-0057).
- Every pod in `monitoring` is `Running`, and Grafana answers on node port 30300
  (pinned in `infrastructure/monitoring.yaml`): it opens with no login on every
  dashboard, offers no way to edit or save one, shows no login form, and refuses
  `admin`/`admin`, there being no admin user: posting them to its login
  answers 401.

    ```bash
    curl -i -X POST -H 'Content-Type: application/json' \
      -d '{"user":"admin","password":"admin"}' http://<node>:30300/login
    ```

- After a rebuild, the node also passes
  [Verify the hardening](#verify-the-hardening).

Then the OS's hardening. From a LAN host that is not one of the maintainer's
addresses, scan the node:

```sh
nmap -Pn -sT -p- --open <node-address>
```

- It shows 30300 alone open. The game ports are UDP, which this scan does not
  show: a client on that host still joins a Match in either environment.

From an admin address, try a password login, root's, and your key:

```sh
ssh -o PubkeyAuthentication=no <user>@<node-address>
ssh -o PubkeyAuthentication=no root@<node-address>
ssh <user>@<node-address> true
kubectl get nodes
```

- The first two are refused with `Permission denied (publickey)`, with no
  password prompt; the key login and `kubectl` work.
- [Updates](#updates)' checks show `Debian-Security` alone, and
  [The audit trail](#the-audit-trail)'s show the login just made, from that
  address.

## Rollback / abort

- Part A changes nothing but the service's state; it can be repeated.
- Part B is not reversible once step 2 has run: the old datastore is gone.
  Nothing in it is needed, since Git holds every deployed object, so the abort
  is to repeat part B from step 3. Do not start step 2 without the packs copied
  (step 1) or a way to re-cook them.

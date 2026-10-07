# CD Strategy: Flux for `main`/`develop`; No k3s Deploy for Ephemeral Branches

`main` and `develop` are reconciled by Flux (GitOps, pull-based) running inside
the k3s cluster. `feature/*`, `hotfix/*`, and `release/*` branches are not
deployed to k3s at all — no CD, ephemeral or otherwise, for these.

**One server per scenario.** An environment runs one `augustad` server for each
scenario it serves, and a server serves its scenario's pack and no other: its
own Deployment of one pod, game Service on its own node port, and metrics
Service, named `augustad-<scenario>`. The node port is pinned, from a range of
the environment's own (`develop` 30700-30799, `staging` 30800-30899), so the two
environments never collide on the node. The servers are a list in the
environment's `HelmRelease` values (`servers`, keyed by scenario name, each
naming the pack version it runs), so one release per environment still owns them
all, and adding a scenario is one entry, not one more release.

**Packs are published, then deployed through Git.** A server pack is not in the
image or the chart: `augusta-publish` (in `tools/pack`) puts it on the node's
shared `hostPath` volume, at `<scenario>/<version>/`, beside the public key it
is signed with, after checking both packs of its cook verify against that key
and that the server pack names that client pack (ADR-0031). The version is the
first 12 hex characters of the server pack's BLAKE3 hash, so a folder is never
written twice: a new cook is a new folder. Publishing changes nothing that runs;
a server takes the pack once its environment's `HelmRelease` names that version,
merged like any other change, and each server mounts only its own version's
folder. A rollback reverts that commit, and the older folder is still there to
go back to. Whoever holds the signing key cooks, publishes and opens that pull
request; nothing in CI or the cluster can, since neither has the key or the
node's shell.

**A rollout may cut a match in progress.** An upgrade replaces a server's pod
the Deployment's default way: the new pod starts, and the old one gets SIGTERM
once the new one is Ready. `augustad` stops within a tick of SIGTERM, so the
chart's grace period is short (10 s) rather than sized for a match to end, and
the chart declares no PodDisruptionBudget: with one replica, the only budget
that protects anything blocks every node drain. Both environments exist for
testing a build, not for players to keep a match through it.

**A liveness probe, no readiness probe.** The chart probes the metrics
endpoint's `/livez` (ADR-0049), which fails once the Simulation thread has gone
5 seconds without finishing a tick, so Kubernetes restarts a server whose tick
loop has hung. `augustad` serves `/livez` only after its pack is verified and
its content loaded, so a `startupProbe` on it holds the `livenessProbe` back
until it first answers, and gives a slow pack load minutes, not seconds, before
restarting the server. The liveness probe can then stay tight, restarting a hung
loop within seconds, with no fixed delay to guess at. There is no readiness
probe: a single replica takes no balanced traffic, so the pod is Ready once its
container runs. A server that fails to start exits (ADR-0005) and shows as a
crash-looping pod.

## Considered Options

The original design (see git history of `docs/ENGINEERING.md`'s Deployment & CD
section) was push-based for every environment: a self-hosted GitHub Actions
runner ran `helm upgrade --install` on push (ephemeral namespace per
`feature/*`/`hotfix/*`/`release/*` branch, long-lived for `develop`) and
`helm uninstall` on branch delete. Production (`main`) deployment was left
undecided.

Revisited once `main` needed an actual decision, and once a self-hosted runner's
exposure became a real concern: this repository is public, and a self-hosted
runner has full access to the host running it (including the k3s cluster) for
every workflow run it executes — including ones triggered by an external
contributor's pull request.

Flux (pull-based, running inside the cluster) removes that exposure for
`main`/`develop`. For the ephemeral branches, the only way to keep the old
preview-per-branch behavior would still need a self-hosted runner (Flux's
reconcile model doesn't map onto create-on-push/destroy-on-delete environments
without extra tooling on top). Rather than accept that runner's attack surface
just for preview environments that aren't essential, ephemeral branches are
simply not deployed to k3s at all.

- **Draining before a rollout** (stop admitting, wait for the match to end, then
  exit, under a grace period as long as a match): rejected for now. It needs a
  drain mode in `augustad` and a grace period with no natural bound (Game policy
  decides when a match ends), to protect matches no one plays to keep. Revisit
  when an environment has players.
- **A readiness probe** (on `/livez`, or on the game port's UDP socket in the
  pod's own socket table): rejected. It would make a rollout wait for the new
  server to load its pack before stopping the old one, but with one replica no
  traffic is balanced away from an unready pod, and a server that never starts
  already shows as a crash-looping pod.

- **One server hosting several scenarios** (a Lobby per scenario in one
  process): rejected. Its packs, parameters and failures would share a process,
  and a pack change would restart every scenario's Match; a server per scenario
  costs only another pod.
- **A `HelmRelease` per environment and scenario**: rejected. The same release
  settings repeated per scenario, for nothing a list in one release's values
  does not give.
- **One folder per scenario, overwritten by each publish**: rejected. The
  previous pack is gone, so nothing is left to roll back to; the running server
  keeps the old pack until something restarts it, since its spec has not
  changed; and from that restart on, clients holding the old client pack are
  refused at Join (ADR-0038).
- **Publishing from CI, or naming the version from the tool** (the tool editing
  the `HelmRelease` and opening the pull request): rejected. CI has neither the
  signing key nor a way into the node (no self-hosted runner), and a tool that
  edits Git would deploy as a side effect of a copy.

## Consequences

- No self-hosted GitHub Actions runner exists in this pipeline at all — removes
  that attack surface entirely, not just for `main`/`develop`.
- `feature/*`, `hotfix/*`, `release/*` branches get no automated deployment;
  verify server changes locally before merging to `develop`.
- M0b's k3s scope is just two fixed Helm releases (`main` → staging namespace,
  `develop` → develop namespace), both owned by Flux, each running a server per
  scenario its values list.
- A server whose pack version is not published yet never starts: its pod waits
  in `ContainerCreating`, its events naming the missing folder.
- `augusta-publish` reaches the node over SSH as a user that can `sudo` without
  a password; the node's address is not in the repository.
- CI (build/test, `helm lint`/`docker build`) is unaffected: it never needed
  cluster access, so it stays on GitHub-hosted runners and just pushes the built
  image to GHCR, which Flux reads from for `main`/ `develop`.
- Every push to `main`/`develop` publishes an image tagged
  `sha-<first 12 characters of the commit>`, and the chart runs the tag of its
  own commit (Flux versions a Git-sourced chart
  `<version>+<those 12 characters>`). A tag is never reused, so a pod never runs
  a stale image, and Flux's upgrade waits for CI to finish pushing it. No
  image-automation controller is needed.
- Without a readiness probe, the new pod is Ready as soon as its container runs,
  so Helm's and Flux's upgrade wait no longer catches a server that fails to
  start or crash-loops after starting: the upgrade succeeds, and the failure
  shows only as the pod's restarts and the server-down alert (ADR-0049).
- The chart requests CPU and memory for the server, limits its memory at that
  request, and sets no CPU limit: a throttled Simulation thread misses ticks
  (NFR-01).

# CD Strategy: Flux for `main`/`develop`; No k3s Deploy for Ephemeral Branches

`main` and `develop` are reconciled by Flux (GitOps, pull-based) running
inside the k3s cluster. `feature/*`, `hotfix/*`, and `release/*` branches
are not deployed to k3s at all — no CD, ephemeral or otherwise, for these.

**A rollout may cut a match in progress.** Each environment runs one
`augustad` pod, and an upgrade replaces it the Deployment's default way:
the new pod starts, and the old one gets SIGTERM once the new one is
Ready. `augustad` stops within a tick of SIGTERM, so the chart's grace
period is short (10 s) rather than sized for a match to end, and the
chart declares no PodDisruptionBudget: with one replica, the only budget
that protects anything blocks every node drain. Both environments exist
for testing a build, not for players to keep a match through it.

**A liveness probe, no readiness probe.** The chart's one probe is a
`livenessProbe` on the metrics endpoint's `/livez` (ADR-0049), which fails
once the Simulation thread has gone 5 seconds without finishing a tick, so
Kubernetes restarts a server whose tick loop has hung. `augustad` serves
`/livez` only after its pack is verified and its content loaded, so the
probe's first check waits that loading out, and a restart takes several
failed checks in a row: a slow start is not taken for a hung loop. There
is no readiness probe: a single replica takes no balanced traffic, so the
pod is Ready once its container runs. A server that fails to start exits
(ADR-0005) and shows as a crash-looping pod.

## Considered Options

The original design (see git history of `docs/ENGINEERING.md`'s
Deployment & CD section) was push-based for every environment: a
self-hosted GitHub Actions runner ran `helm upgrade --install` on push
(ephemeral namespace per `feature/*`/`hotfix/*`/`release/*` branch,
long-lived for `develop`) and `helm uninstall` on branch delete.
Production (`main`) deployment was left undecided.

Revisited once `main` needed an actual decision, and once a self-hosted
runner's exposure became a real concern: this repository is public, and
a self-hosted runner has full access to the host running it (including
the k3s cluster) for every workflow run it executes — including ones
triggered by an external contributor's pull request.

Flux (pull-based, running inside the cluster) removes that exposure for
`main`/`develop`. For the ephemeral branches, the only way to keep the
old preview-per-branch behavior would still need a self-hosted runner
(Flux's reconcile model doesn't map onto create-on-push/destroy-on-delete
environments without extra tooling on top). Rather than accept that
runner's attack surface just for preview environments that aren't
essential, ephemeral branches are simply not deployed to k3s at all.

- **Draining before a rollout** (stop admitting, wait for the match to
  end, then exit, under a grace period as long as a match): rejected for
  now. It needs a drain mode in `augustad` and a grace period with no
  natural bound (Game policy decides when a match ends), to protect
  matches no one plays to keep. Revisit when an environment has players.
- **A readiness probe** (on `/livez`, or on the game port's UDP socket in
  the pod's own socket table): rejected. It would make a rollout wait for
  the new server to load its pack before stopping the old one, but with one
  replica no traffic is balanced away from an unready pod, and a server
  that never starts already shows as a crash-looping pod.

## Consequences

- No self-hosted GitHub Actions runner exists in this pipeline at all —
  removes that attack surface entirely, not just for `main`/`develop`.
- `feature/*`, `hotfix/*`, `release/*` branches get no automated
  deployment; verify server changes locally before merging to `develop`.
- M0b's k3s scope is just two fixed Helm releases (`main` → staging
  namespace, `develop` → develop namespace), both owned by Flux.
- CI (build/test, `helm lint`/`docker build`) is unaffected: it never
  needed cluster access, so it stays on GitHub-hosted runners and just
  pushes the built image to GHCR, which Flux reads from for `main`/
  `develop`.
- Every push to `main`/`develop` publishes an image tagged
  `sha-<first 12 characters of the commit>`, and the chart runs the tag
  of its own commit (Flux versions a Git-sourced chart
  `<version>+<those 12 characters>`). A tag is never reused, so a pod
  never runs a stale image, and Flux's upgrade waits for CI to finish
  pushing it. No image-automation controller is needed.
- The chart requests CPU and memory for the server, limits its memory at
  that request, and sets no CPU limit: a throttled Simulation thread
  misses ticks (NFR-01).

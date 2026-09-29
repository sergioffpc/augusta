# CD Strategy: Flux for `main`/`develop`; No k3s Deploy for Ephemeral Branches

`main` and `develop` are reconciled by Flux (GitOps, pull-based) running
inside the k3s cluster. `feature/*`, `hotfix/*`, and `release/*` branches
are not deployed to k3s at all — no CD, ephemeral or otherwise, for these.

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

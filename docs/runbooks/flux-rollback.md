# Roll Back a Bad Deploy with Flux

Returns the `develop` or `staging` environment to a working server after a
commit that Flux deployed broke it. How the environments are deployed is
ADR-0026 ([CD strategy](../adr/0026-cd-strategy.md)) and
[ENGINEERING.md's Deployment & CD](../ENGINEERING.md#deployment-cd).

## When to use

- The `augustad` pod of an environment crash-loops, fails to start, or
  misbehaves after a merge to `develop` (the `develop` namespace) or `main`
  (the `staging` namespace).
- A Flux `HelmRelease` is stuck failing after a chart or
  `clusters/onprem/apps/` change.

Not for a pod that crash-loops because its pack or public key is missing or
does not verify (the server's last log line names `server.pack` or
`augusta.pub`): no rollback fixes that, the files must be put in place
([Where packs go on the node](pack-key-rotation.md#where-packs-go-on-the-node)).
Moving an environment back to its previous pack folder is the rollback step of
[Rotate the Pack Signing Key](pack-key-rotation.md#rollback-abort).

## What runs where

| Environment | HelmRelease (namespace `flux-system`) | Helm release | Namespace | Chart from | Image tag |
|---|---|---|---|---|---|
| develop | `augustad-develop` | `augustad` | `develop` | GitRepository `augusta-develop` (`develop`) | `sha-<12>` of the chart's commit |
| staging | `augustad-staging` | `augustad` | `staging` | GitRepository `augusta-main` (`main`) | `main` (`image.tag` in `staging.yaml`) |

Both `HelmRelease` objects, and the rest of
[`clusters/onprem/apps/`](../../clusters/onprem/apps), are applied by the
`apps` Kustomization from the `flux-system` GitRepository, which tracks
`develop` ([`gotk-sync.yaml`](../../clusters/onprem/flux-system/gotk-sync.yaml)).
A change to `staging.yaml` therefore deploys when it reaches `develop`, not
`main`. Neither `HelmRelease` sets `upgrade.remediation`, so Flux never rolls
back by itself: a failed upgrade stays failed until Git or a person changes it.

The two environments roll back differently. `develop` runs the image of its
chart's commit, so going back to an earlier commit, by Helm or by Git, goes
back to its image. `staging` runs the mutable `main` tag with
`pullPolicy: IfNotPresent`: a Helm rollback or a revert on `main` leaves the
pod's image string `augustad:main` unchanged, so nothing rolls out, and the
node keeps whichever `main` image it has cached. `staging` is rolled back by
pinning its image to the last good commit's `sha-<12>` tag instead.

## Prerequisites

- A kubeconfig for the k3s cluster. The repository does not record how one is
  obtained; step 4 of
  [Recover or Rebuild the k3s Node](k3s-node-recovery.md#part-b-rebuild) copies
  it from the node.
- `kubectl` and `helm`: `scripts/bootstrap-wsl.sh` installs both.
- The `flux` CLI at the cluster's Flux version, the `Flux Version` in
  [`gotk-components.yaml`](../../clusters/onprem/flux-system/gotk-components.yaml)'s
  header. No bootstrap script installs it.
- `gh`, authenticated, with permission to merge pull requests to `develop` and
  `main` ([branch rules](cut-release.md#prerequisites)).

## Steps

1. Find what is failing and which revision Flux last applied:

    ```sh
    flux get sources git -n flux-system
    flux get helmreleases -n flux-system
    flux logs --kind=HelmRelease --name=augustad-develop -n flux-system   # or augustad-staging
    kubectl -n develop get pods                                            # or -n staging
    kubectl -n develop logs deploy/augustad --previous
    ```

    The `HelmRelease` revision is `<chart version>+<12 characters of the
    commit>`: that commit is the one deployed. Note the commit of the bad
    merge, `<bad-merge>`, and the environment's last good commit, `<good>`.

2. Continue with [Roll back develop](#roll-back-develop) or
   [Roll back staging](#roll-back-staging).

### Roll back develop

1. If the failure is an upgrade still waiting for its image (`timeout: 30m` in
   `develop.yaml` covers CI still pushing `sha-<12>`), check the `container`
   job of that commit's CI run first:

    ```sh
    gh run list --workflow ci.yml --branch develop --limit 5
    ```

    A failed `container` job (build or trivy scan) means the image was never
    published; the fix is the rollback below, or a new commit that publishes
    one.

2. **Restore service now (optional, when the environment cannot wait for
   CI).** Stop Flux from reconciling the release, then roll Helm back to the
   last good revision:

    ```sh
    flux suspend helmrelease augustad-develop -n flux-system
    helm history augustad -n develop
    helm rollback augustad <good-revision> -n develop --wait --timeout 10m
    ```

    The Helm release record lives in the environment's own namespace
    (`storageNamespace`). Leave the `HelmRelease` suspended until step 4 has
    merged; resuming it earlier upgrades straight back to the bad commit.

3. Revert the bad change on a `feature/*` branch off `develop`:

    ```sh
    git fetch origin
    git switch -c feature/revert-<topic> origin/develop
    git revert --no-commit -m 1 <bad-merge>   # drop -m 1 for a non-merge commit
    git commit -m "revert: <what is reverted>"
    git push -u origin feature/revert-<topic>
    gh pr create --base develop --title "revert: <what is reverted>" --body "<why>"
    ```

    `--no-commit` and a message of your own are needed because git's default
    `Revert "..."` message fails the `commit-msg` hook's Conventional Commits
    check. A bad `clusters/onprem/apps/` change, `staging.yaml` included, is
    reverted here too (see [What runs where](#what-runs-where)).

4. Merge the pull request once its checks pass:

    ```sh
    gh pr merge <number> --merge
    ```

5. If step 2 suspended the release, resume it, and make Flux pick up the revert
   without waiting for its interval:

    ```sh
    flux resume helmrelease augustad-develop -n flux-system
    flux reconcile source git augusta-develop -n flux-system
    flux reconcile helmrelease augustad-develop -n flux-system
    ```

    For a `clusters/onprem/apps/` revert, reconcile the Kustomization instead:
    `flux reconcile kustomization apps -n flux-system --with-source`. The
    upgrade waits for CI to publish the revert commit's `sha-<12>` image before
    the pod changes.

### Roll back staging

1. Pin `staging` to the last good `main` commit's image. CI published
   `ghcr.io/sergioffpc/augustad:sha-<good12>` (the first 12 characters of
   `<good>`) when that commit was pushed to `main`. Check it exists, then pin
   it on a `feature/*` branch off `develop`:

    ```sh
    docker buildx imagetools inspect ghcr.io/sergioffpc/augustad:sha-<good12>
    git fetch origin
    git switch -c feature/pin-staging-<good12> origin/develop
    ```

    In [`clusters/onprem/apps/staging.yaml`](../../clusters/onprem/apps/staging.yaml),
    set `spec.values.image.tag` to `sha-<good12>`, then:

    ```sh
    git commit -am "revert(cluster): pin staging to the sha-<good12> image"
    git push -u origin feature/pin-staging-<good12>
    gh pr create --base develop --title "revert(cluster): pin staging to the sha-<good12> image" --body "<why>"
    gh pr merge <number> --merge          # once its checks pass
    flux reconcile kustomization apps -n flux-system --with-source
    ```

    The pod's image string changes, so the Deployment rolls out the pinned
    image. The chart still comes from `main`'s head: if the bad change is in
    the chart rather than the image, also suspend and roll back Helm as in
    [Roll back develop](#roll-back-develop) step 2, with `augustad-staging` and
    `-n staging`, and keep the `HelmRelease` suspended until step 3 below.

2. Fix `main`: revert or fix the bad change on a `hotfix/*` branch off
   `origin/main`, as in [Roll back develop](#roll-back-develop) step 3 but with
   `--base main`. After it merges, bring the same branch back into `develop`
   with a second pull request, `--base develop`, as Git Flow does for every
   hotfix. When the bad change is a published release, the fix ships as the
   next patch release ([Cut a Release](cut-release.md#rollback-abort)).

3. Remove the pin once `main`'s head is good: on a `feature/*` branch off
   `develop`, delete the `image.tag` line from `staging.yaml` and merge it
   through a pull request. Then resume the `HelmRelease` if step 1 suspended it,
   and reconcile:

    ```sh
    flux resume helmrelease augustad-staging -n flux-system
    flux reconcile kustomization apps -n flux-system --with-source
    ```

    With `image.tag` empty, `main`'s chart runs the `sha-<12>` image of its own
    commit, as `develop`'s does (`staging.yaml`'s own comment asks for the line
    to go). Setting it back to `main` instead would let the node keep running a
    stale cached `main` image.

## Verification

```sh
flux get helmreleases -n flux-system
kubectl -n develop rollout status deploy/augustad --timeout 10m        # or -n staging
kubectl -n develop get deploy augustad -o jsonpath='{.spec.template.spec.containers[0].image}'
kubectl -n develop logs deploy/augustad | grep 'event=pack_verified'
```

- The `HelmRelease` is `Ready` and not suspended.
- On `develop`, its revision's `+<12>` is the revert commit (or later), and the
  image is `ghcr.io/sergioffpc/augustad:sha-<12>` of that same commit.
- On `staging`, the image is the pinned `sha-<good12>`, or, once the pin is
  removed, `sha-<12>` of `main`'s head.
- The server logged `event=pack_verified` and keeps running (the pod's restart
  count stays put).

## Rollback / abort

- Before any pull request merges, nothing has changed in Git:
  `flux resume helmrelease <name> -n flux-system` hands the release back to
  Flux, which upgrades it to the branch head again.
- A revert or pin that turns out wrong is itself reverted the same way.
- Never leave a `HelmRelease` suspended: `flux get helmreleases -n flux-system`
  shows `SUSPENDED True` until it is resumed, and no later commit deploys.

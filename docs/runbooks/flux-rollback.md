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

A pod that crash-loops because its pack or public key is missing or does not
verify (the server's last log line names `server.pack` or `augusta.pub`) is not
a deploy either: no rollback fixes it, the files must be put in place (see
[Where packs go on the node](pack-key-rotation.md#where-packs-go-on-the-node)).
A bad asset pack is not a deploy: packs reach the node by hand, and moving an
environment back to its previous pack folder is the rollback step of
[Rotate the Pack Signing Key](pack-key-rotation.md).

## What runs where

| Environment | HelmRelease (namespace `flux-system`) | Helm release | Namespace | Chart from | Image tag |
|---|---|---|---|---|---|
| develop | `augustad-develop` | `augustad` | `develop` | GitRepository `augusta-develop` (`develop`) | `sha-<12>` of the chart's commit |
| staging | `augustad-staging` | `augustad` | `staging` | GitRepository `augusta-main` (`main`) | `main` (set in `staging.yaml`) |

Both `HelmRelease` objects, and the rest of
[`clusters/onprem/apps/`](../../clusters/onprem/apps), are applied by the
`apps` Kustomization from the `flux-system` GitRepository, which tracks
`develop` ([`gotk-sync.yaml`](../../clusters/onprem/flux-system/gotk-sync.yaml)).
A change to `staging.yaml` therefore deploys when it reaches `develop`, not
`main`. Neither `HelmRelease` sets `upgrade.remediation`, so Flux never rolls
back by itself: a failed upgrade stays failed until Git or a person changes it.

## Prerequisites

- A kubeconfig for the k3s cluster. The repository does not record how one is
  obtained; [Recover or Rebuild the k3s Node](k3s-node-recovery.md) shows how to
  copy it from the node.
- `kubectl` and `helm`: `scripts/bootstrap-wsl.sh` installs both.
- The `flux` CLI, matching the cluster's Flux (v2.9.5, the version in
  [`gotk-components.yaml`](../../clusters/onprem/flux-system/gotk-components.yaml)'s
  header). No bootstrap script installs it.
- `gh`, authenticated, with permission to merge pull requests to `develop` and
  `main`. Both branches require a pull request, signed commits, and the
  `sanitizers`, `changes`, `format`, `client` and `server` checks.

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
    merge, `<bad-merge>`, and the environment's last good one.

2. If the failure is an upgrade still waiting for its image (develop only:
   `timeout: 30m` covers CI still pushing `sha-<12>`), check the `container`
   job of that commit's CI run before rolling back:

    ```sh
    gh run list --workflow ci.yml --branch develop --limit 5
    ```

    A failed `container` job (build or trivy scan) means the image was never
    published; the fix is the rollback below, or a new commit that publishes
    one.

3. **Restore service now (optional, when the environment cannot wait for CI).**
   Stop Flux from reconciling the release, then roll Helm back to the last good
   revision:

    ```sh
    flux suspend helmrelease augustad-develop -n flux-system
    helm history augustad -n develop
    helm rollback augustad <good-revision> -n develop --wait --timeout 10m
    ```

    The Helm release record lives in the environment's own namespace
    (`storageNamespace`), so `-n develop` / `-n staging` is the right one. Leave
    the `HelmRelease` suspended until step 5 has merged; resuming it earlier
    upgrades straight back to the bad commit.

4. Revert the bad change in Git. For `develop`, on a `feature/*` branch:

    ```sh
    git fetch origin
    git switch -c feature/revert-<topic> origin/develop
    git revert --no-commit -m 1 <bad-merge>   # drop -m 1 for a non-merge commit
    git commit -m "revert: <what is reverted>"
    git push -u origin feature/revert-<topic>
    gh pr create --base develop --title "revert: <what is reverted>" --body "<why>"
    ```

    For `staging` (`main`), the same on a `hotfix/*` branch off `origin/main`,
    with `--base main`; after it merges, bring it back to `develop` with a pull
    request from the same branch with `--base develop`, as Git Flow does for
    every hotfix.

    `--no-commit` and a message of your own are needed because git's default
    `Revert "..."` message fails the `commit-msg` hook's Conventional Commits
    check.

    A bad `clusters/onprem/apps/` change, `staging.yaml` included, is always
    reverted on `develop` (see What runs where).

5. Merge the pull request once its checks pass:

    ```sh
    gh pr merge <number> --merge
    ```

6. If step 3 suspended the release, resume it, and make Flux pick up the revert
   without waiting for its interval:

    ```sh
    flux resume helmrelease augustad-develop -n flux-system
    flux reconcile source git augusta-develop -n flux-system
    flux reconcile helmrelease augustad-develop -n flux-system
    ```

    For a `clusters/onprem/apps/` revert, reconcile the Kustomization instead:
    `flux reconcile kustomization apps -n flux-system --with-source`.

    On `develop`, the upgrade waits for CI to publish the revert commit's
    `sha-<12>` image before the pod changes.

## Verification

```sh
flux get helmreleases -n flux-system
kubectl -n develop rollout status deploy/augustad --timeout 10m
kubectl -n develop get deploy augustad -o jsonpath='{.spec.template.spec.containers[0].image}'
kubectl -n develop logs deploy/augustad | grep 'event=pack_verified'
```

- The `HelmRelease` is `Ready` at a revision whose `+<12>` is the revert commit
  (or later), and is not suspended.
- On `develop`, the image is `ghcr.io/sergioffpc/augustad:sha-<12>` of that same
  commit.
- The server logged `event=pack_verified` and keeps running (the pod's restart
  count stays put).

`staging` runs the mutable `main` tag with `pullPolicy: IfNotPresent`, so the
node can keep running a cached `main` image after `main` moves. Compare the
running image's digest with the one GHCR serves for `sha-<12>` of `main`'s
head before calling `staging` rolled back:

```sh
kubectl -n staging get pods -l app.kubernetes.io/name=augustad \
  -o jsonpath='{.items[0].status.containerStatuses[0].imageID}'
```

## Rollback / abort

- Before step 4 nothing has changed in Git: `flux resume helmrelease
  <name> -n flux-system` hands the release back to Flux, which upgrades it to
  the branch head again.
- A revert that turns out wrong is itself reverted the same way (steps 4 to 6).
- Never leave a `HelmRelease` suspended: `flux get helmreleases -n flux-system`
  shows `SUSPENDED True` until it is resumed, and no later commit deploys.

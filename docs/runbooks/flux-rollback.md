# Roll Back a Bad Deploy with Flux

Returns the `develop` or `staging` environment to a working server after a
commit that Flux deployed broke it. How the environments are deployed is
ADR-0026 ([CD strategy](../adr/0026-cd-strategy.md)), ADR-0056
([signed revisions](../adr/0056-signed-revisions-for-flux.md)) and
[ENGINEERING.md's Deployment & CD](../ENGINEERING.md#deployment--cd).

## When to use

- The `augustad` pod of an environment crash-loops, fails to start, or
  misbehaves after a merge to `develop` (the `develop` namespace) or a release
  tag (the `staging` namespace).
- A Flux `HelmRelease` is stuck failing after a chart or `clusters/onprem/apps/`
  change.

Not for a pod that crash-loops because its pack or public key is missing or does
not verify (the server's last log line names `server.apak` or `signing.pub`): no
rollback fixes that, the files must be put in place
([Where packs go on the node](pack-key-rotation.md#where-packs-go-on-the-node)).
Moving an environment back to its previous pack folder is the rollback step of
[Rotate the Pack Signing Key](pack-key-rotation.md#rollback--abort).

Nor for a source that fails verification (`flux get sources git -n flux-system`
or `flux get sources oci -n flux-system` shows it not ready, its message naming
the signature): Flux deployed nothing from that revision, and its environment
keeps running the last verified one. The fix is a signed revision, or the
trusted keys in
[`trusted-signers.yaml`](../../clusters/onprem/flux-system/trusted-signers.yaml)
updated after a key rotation (ADR-0056).

Nor for a `develop` commit whose CI failed: CI publishes `develop`'s chart only
once the commit's image is published (its `chart-publish` job), so `develop`
keeps running the previous chart. The fix is a new commit whose CI passes.

## What runs where

| Environment | HelmRelease (namespace `flux-system`) | Helm release | Namespace | Chart from                                                | Image tag                        |
| ----------- | ------------------------------------- | ------------ | --------- | --------------------------------------------------------- | -------------------------------- |
| develop     | `augustad-develop`                    | `augustad`   | `develop` | OCIRepository `augustad-develop` (tag `develop`, from CI) | `sha-<12>` of the chart's commit |
| staging     | `augustad-staging`                    | `augustad`   | `staging` | GitRepository `augusta-release` (newest `v*` tag)         | `sha-<12>` of the chart's commit |

`develop`'s values come from Git, the GitRepository `augusta-develop`
(`develop`'s HEAD), not from its chart. Both `HelmRelease` objects, every source
([`sources.yaml`](../../clusters/onprem/apps/sources.yaml)) and the rest of
[`clusters/onprem/apps/`](../../clusters/onprem/apps), are applied by the `apps`
Kustomization from the `flux-system` GitRepository, which tracks `develop`
([`gotk-sync.yaml`](../../clusters/onprem/flux-system/gotk-sync.yaml)). A change
to `staging.yaml` or `sources.yaml` therefore deploys when it reaches `develop`,
not `main`. Neither `HelmRelease` sets `upgrade.remediation`, so Flux never
rolls back by itself: a failed upgrade stays failed until Git or a person
changes it.

Both environments run the image CI published for their chart's own commit
(`image.tag` is left empty, [`values.yaml`](../../charts/augustad/values.yaml)),
so going back to an earlier chart, by Helm or by Git, goes back to its image.
Both go back by pinning their chart's source in Git to an older immutable tag:
`develop`'s to the chart CI published for an earlier commit,
`<chart version>_<12>`, and `staging`'s to the previous release tag, since a
published tag is never moved or reused. A bad change to an environment's values
alone is reverted on `develop` instead.

## Prerequisites

- The day-to-day kubeconfig, from one of the maintainer's addresses
  ([Cluster access](k3s-node-recovery.md#cluster-access)): every step but the
  Helm rollback (step 1 of [Roll back develop](#roll-back-develop)) needs no
  more. That step is **cluster-admin**, and needs SSH to the node as a user with
  `sudo` ([Use cluster-admin](k3s-node-recovery.md#use-cluster-admin)).
- `kubectl`, `helm` and the `flux` CLI at the cluster's Flux version, the
  `Flux Version` in
  [`gotk-components.yaml`](../../clusters/onprem/flux-system/gotk-components.yaml)'s
  header: `scripts/bootstrap.sh` installs `kubectl` and `helm`, and `flux` is
  the toolchain's, at the `FLUX_VERSION`
  [`versions.env`](../../scripts/bootstrap/versions.env) pins. Run
  `make toolchain` in this repository's checkout, then
  `source .toolchain/activate` in the shell that runs the steps, so `flux` is
  that one.
- `gh`, authenticated, with permission to merge pull requests to `develop` and
  `main` ([branch rules](cut-release.md#prerequisites)).

## Steps

1. Find what is failing and which revision Flux last applied:

    ```sh
    flux get sources git -n flux-system
    flux get sources oci -n flux-system
    flux get helmreleases -n flux-system
    flux logs --kind=HelmRelease --name=augustad-develop -n flux-system   # or augustad-staging
    kubectl -n develop get pods                                            # or -n staging
    kubectl -n develop logs -l app.kubernetes.io/name=augustad --prefix --previous
    ```

    The `HelmRelease` revision is
    `<chart version>+<12 characters of the commit>`: that commit's chart is the
    one deployed. Note the commit of the bad merge, `<bad-merge>`, and the
    environment's last good revision, `<chart version>+<good>`.

2. Continue with [Roll back develop](#roll-back-develop) or
   [Roll back staging](#roll-back-staging).

### Roll back develop

`develop` runs the chart CI published for the branch's newest commit, the
`OCIRepository` `augustad-develop`'s tag `develop`, so a bad chart or image is
rolled back by pinning that source to the last good commit's chart, tagged
`<chart version>_<good>` (Helm's `_` for `+`), and the fix ships as a new commit
on `develop`. A bad change to `develop.yaml`'s values alone needs no pin: revert
it as step 3 does, and it rolls out without waiting for CI.

1. **Restore service now (optional, when the environment cannot wait for a pull
   request).** Stop Flux from reconciling the release, then roll Helm back to
   the last good revision. The rollback is **cluster-admin**: Helm reads its
   release records, which are Secrets, and changes the Deployment itself
   ([Use cluster-admin](k3s-node-recovery.md#use-cluster-admin)):

    ```sh
    flux suspend helmrelease augustad-develop -n flux-system
    export KUBECONFIG="$(mktemp)"
    ssh <node> "sudo cat /etc/rancher/k3s/k3s.yaml" \
      | sed 's#https://127.0.0.1:6443#https://<node-address>:6443#' > "${KUBECONFIG}"
    helm history augustad -n develop
    helm rollback augustad <good-revision> -n develop --wait --timeout 10m
    rm "${KUBECONFIG}"
    export KUBECONFIG=~/.kube/augusta.yaml
    ```

    The Helm release record lives in the environment's own namespace
    (`storageNamespace`). Leave the `HelmRelease` suspended until step 2 has
    merged; resuming it earlier upgrades straight back to the bad chart.

2. Pin the chart on a `feature/*` branch off `develop`: `sources.yaml` is
   applied from `develop` (see [What runs where](#what-runs-where)). In
   [`sources.yaml`](../../clusters/onprem/apps/sources.yaml), replace
   `augustad-develop`'s `tag: develop` with `tag: <chart version>_<good>`. Flux
   still verifies the pinned chart's signature.

    ```sh
    git fetch origin
    git switch -c feature/pin-develop-<good> origin/develop
    # edit clusters/onprem/apps/sources.yaml: ref: {tag: <chart version>_<good>}
    git commit -am "fix(cluster): pin develop to <good>'s chart"
    git push -u origin feature/pin-develop-<good>
    gh pr create --base develop --title "fix(cluster): pin develop to <good>'s chart" --body "<why>"
    ```

    Merge it once its checks pass (`gh pr merge <number> --merge`), resume the
    release if step 1 suspended it, and reconcile:

    ```sh
    flux resume helmrelease augustad-develop -n flux-system
    flux reconcile kustomization apps -n flux-system --with-source
    flux reconcile source oci augustad-develop -n flux-system
    flux reconcile helmrelease augustad-develop -n flux-system
    ```

    `<good>`'s image, `sha-<good>`, was published before its chart, so the pod
    changes without waiting for CI. Until the pin is removed (step 5), no new
    commit's chart deploys to `develop`; its values still do.

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

    A revert of `clusters/onprem/apps/` alone deploys at once
    (`flux reconcile kustomization apps -n flux-system --with-source`): if
    nothing was pinned, it is the whole rollback.

5. Once CI has published the revert commit's chart (its `chart-publish` job
   passed, and `develop`'s CI has not moved on since), remove the pin the same
   way as step 2 pinned it, `tag: develop` back in place of
   `tag: <chart version>_<good>`, and reconcile as step 2 does. Removed earlier,
   the tag `develop` would still name the bad chart.

### Roll back staging

`staging` runs the newest `v*` tag, so a bad release is rolled back by pinning
its source, `augusta-release`, to the last good tag, `vGOOD`, and the fix ships
as the next patch release ([Cut a Release](cut-release.md#rollback--abort)).

1. **Restore service now (optional)** as step 1 of
   [Roll back develop](#roll-back-develop) does, with `augustad-staging` and
   `-n staging`.

2. Pin the source on a `feature/*` branch off `develop`: `sources.yaml` is
   applied from `develop` (see [What runs where](#what-runs-where)). In
   [`sources.yaml`](../../clusters/onprem/apps/sources.yaml), replace
   `augusta-release`'s `semver: "*"` with `tag: vGOOD`; a `semver` left beside
   it would win. Flux still verifies the pinned tag's signature.

    ```sh
    git fetch origin --tags
    git switch -c feature/pin-staging-vGOOD origin/develop
    # edit clusters/onprem/apps/sources.yaml: ref: {tag: vGOOD}
    git commit -am "fix(cluster): pin staging to vGOOD"
    git push -u origin feature/pin-staging-vGOOD
    gh pr create --base develop --title "fix(cluster): pin staging to vGOOD" --body "<why>"
    ```

    A bad `staging.yaml` change (its values, not the chart) is reverted on
    `develop` instead, as step 3 of [Roll back develop](#roll-back-develop)
    does.

3. Merge it once its checks pass (`gh pr merge <number> --merge`), resume the
   release if step 1 suspended it, and reconcile:

    ```sh
    flux resume helmrelease augustad-staging -n flux-system
    flux reconcile kustomization apps -n flux-system --with-source
    flux reconcile source git augusta-release -n flux-system
    flux reconcile helmrelease augustad-staging -n flux-system
    ```

    `vGOOD`'s image, `sha-<12>` of its commit, was published when that commit
    reached `main`, so the pod changes without waiting for CI.

4. Once the fixed release's tag is pushed, remove the pin the same way,
   `semver: "*"` back in place of `tag: vGOOD`. Until then no new tag deploys.

## Verification

```sh
flux get helmreleases -n flux-system
kubectl -n develop rollout status deploy -l app.kubernetes.io/name=augustad --timeout 10m   # or -n staging
kubectl -n develop get deploy -l app.kubernetes.io/name=augustad -o jsonpath='{range .items[*]}{.metadata.name} {.spec.template.spec.containers[0].image}{"\n"}{end}'
kubectl -n develop logs -l app.kubernetes.io/name=augustad --prefix | grep 'event=pack_verified'
```

- The `HelmRelease` is `Ready` and not suspended.
- Its revision's `+<12>` is the pinned chart's commit, or once `develop`'s pin
  is removed the revert commit (or later), and the image is
  `ghcr.io/sergioffpc/augustad:sha-<12>` of that same commit.
- `flux get sources git -n flux-system` and
  `flux get sources oci -n flux-system` show the sources ready, on verified
  revisions.
- The server logged `event=pack_verified` and keeps running (the pod's restart
  count stays put).

## Rollback / abort

- Before any pull request merges, nothing has changed in Git:
  `flux resume helmrelease <name> -n flux-system` hands the release back to
  Flux, which upgrades it to its source's revision again: `develop`'s newest
  published chart, or `staging`'s newest tag.
- A revert or pin that turns out wrong is itself reverted the same way.
- Never leave a `HelmRelease` suspended: `flux get helmreleases -n flux-system`
  shows `SUSPENDED True` until it is resumed, and no later commit deploys.

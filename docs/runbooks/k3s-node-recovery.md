# Recover or Rebuild the k3s Node

Brings the single-node k3s cluster that runs the `develop` and `staging`
environments back up, either by recovering the running install or by
rebuilding it from the repository. The cluster's design is ADR-0026
([CD strategy](../adr/0026-cd-strategy.md)) and
[ENGINEERING.md's Deployment & CD](../ENGINEERING.md#deployment-cd).

## When to use

- The node is up but `kubectl` cannot reach the cluster, the node is
  `NotReady`, or Flux's controllers are not running: **Recover** (part A).
- k3s will not start after part A, its datastore is corrupt, or the node's OS
  or disk was replaced: **Rebuild** (part B).

## What the cluster holds

Everything Flux deploys is in Git: Flux's own manifests in
[`clusters/onprem/flux-system/`](../../clusters/onprem/flux-system), the
monitoring stack's `HelmRelease` in
[`clusters/onprem/infrastructure/`](../../clusters/onprem/infrastructure), the
two augustad `HelmRelease` objects and their GitRepository sources in
[`clusters/onprem/apps/`](../../clusters/onprem/apps), and the chart in
[`charts/augustad/`](../../charts/augustad). The `flux-system` GitRepository
reads the public repository over HTTPS with no credentials, so a rebuilt
cluster needs no deploy key.

The only state not in Git is the asset packs on the node's shared volume:
`/srv/augusta/asset-packs/<packVersion>/server.pack` and `augusta.pub` for each
`packVersion` the `HelmRelease` values in `clusters/onprem/apps/` name
([`charts/augustad/values.yaml`](../../charts/augustad/values.yaml)). Without
them the servers crash-loop; the layout, and why a missing folder still lets
the pod schedule, is
[Where packs go on the node](pack-key-rotation.md#where-packs-go-on-the-node).

Prometheus keeps its last 15 days of series on a `local-path` volume on the
node (ADR-0049). A rebuild loses them, and Prometheus starts again empty; no
step restores them.

## Prerequisites

- Console or SSH access, with `sudo`, to the node (`<node>` below). The
  repository does not record its address or OS.
- The Kubernetes version the cluster runs, `<k8s-version>` below:
  `KUBERNETES_VERSION` in the `manifests` job of
  [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml), which CI
  validates the manifests against. The repository does not record the k3s release (`v<version>+k3s<n>`) or any
  k3s install options or `/etc/rancher/k3s/config.yaml`; the steps below use
  k3s's defaults.
- For a rebuild, a copy of every environment's pack folder: either taken from
  the node in step B1, or re-made from the pack environment that cooked them
  (`<AssetsRoot>\packs\<scenario>\server.pack` and the public key it was
  signed with, see [Rotate the Pack Signing Key](pack-key-rotation.md)).
  The repository defines no backup of the volume.
- The server image, `ghcr.io/sergioffpc/augustad`, pullable without
  credentials: the chart sets no `imagePullSecrets`, so the GHCR package must
  be public.
- On the workstation: `kubectl` and the `flux` CLI
  ([as for a rollback](flux-rollback.md#prerequisites)), and this repository
  with `origin/develop` fetched: the `flux-system` GitRepository tracks
  `develop`.

## Part A: Recover

1. Check the k3s service and its log on the node:

    ```sh
    ssh <node>
    sudo systemctl status k3s
    sudo journalctl -u k3s --since "1 hour ago" --no-pager | tail -n 200
    df -h / /var/lib/rancher /srv/augusta
    ```

    A full disk (DiskPressure, evicted pods, a datastore that cannot write) is
    fixed by freeing space before anything else, for example
    `sudo k3s crictl rmi --prune` for unused images.

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
   install script; it removes `/etc/rancher/k3s` and `/var/lib/rancher/k3s`, not
   `/srv/augusta`:

    ```sh
    ssh <node> "sudo /usr/local/bin/k3s-uninstall.sh"
    ```

    On a reinstalled OS there is nothing to remove; skip this step.

3. Install k3s at the cluster's Kubernetes version, with k3s's install script:

    ```sh
    ssh <node> "curl -sfL https://get.k3s.io | sudo INSTALL_K3S_VERSION='v<k8s-version>+k3s<n>' sh -"
    ssh <node> "sudo k3s kubectl get nodes"
    ```

    Wait until the node is `Ready`.

4. Copy the kubeconfig to the workstation and point it at the node:

    ```sh
    ssh <node> "sudo cat /etc/rancher/k3s/k3s.yaml" > ~/.kube/augusta.yaml
    sed -i 's#https://127.0.0.1:6443#https://<node-address>:6443#' ~/.kube/augusta.yaml
    export KUBECONFIG=~/.kube/augusta.yaml
    kubectl get nodes
    ```

5. Put the asset packs back before Flux starts the servers:

    ```sh
    scp asset-packs.tgz <node>:/tmp/
    ssh <node> "sudo mkdir -p /srv/augusta && sudo tar -C /srv/augusta -xzf /tmp/asset-packs.tgz && rm /tmp/asset-packs.tgz"
    ssh <node> "ls -lR /srv/augusta/asset-packs"
    ```

    Without a copy, re-create each folder as step 6 of
    [Rotate the Pack Signing Key](pack-key-rotation.md) does, one per
    `packVersion` that `git grep -n packVersion -- clusters/` lists, each
    holding `server.pack` and the `augusta.pub` it was signed with. If the
    server pack is still at hand but the folder name is new, copy it under the
    `packVersion` Git names: the folder name, not the pack, is what the chart
    looks for.

6. Install Flux from the repository's own manifests as `develop` has them,
   read straight from `origin/develop` so the checkout's branch is left alone:
   its controllers first, then the sync objects that point it at the
   repository:

    ```sh
    git fetch origin
    git show origin/develop:clusters/onprem/flux-system/gotk-components.yaml | kubectl apply --server-side -f -
    kubectl -n flux-system wait --for=condition=Available deployment --all --timeout 5m
    git show origin/develop:clusters/onprem/flux-system/gotk-sync.yaml | kubectl apply -f -
    ```

    From here Flux manages itself (the `flux-system` Kustomization applies
    `clusters/onprem/flux-system/`), then the `apps` Kustomization, which
    depends on it, installs both environments.

7. Watch it converge:

    ```sh
    flux get kustomizations -n flux-system --watch
    flux get helmreleases -n flux-system
    ```

## Verification

```sh
flux check
flux get all -n flux-system
kubectl get nodes
kubectl -n develop rollout status deploy/augustad --timeout 30m
kubectl -n staging rollout status deploy/augustad --timeout 30m
kubectl -n develop logs deploy/augustad | grep 'event=pack_verified'
kubectl -n staging logs deploy/augustad | grep 'event=pack_verified'
kubectl get svc -A -l app.kubernetes.io/name=augustad
kubectl -n monitoring get pods
```

- The node is `Ready`; every Flux source, Kustomization and `HelmRelease` is
  `Ready`.
- Both servers logged `event=pack_verified` and keep running.
- `develop`'s Service is on node port 30777 (pinned in `develop.yaml`).
  `staging`'s node port is assigned by Kubernetes, so a rebuild changes it:
  give LAN clients the new one from the `get svc` command.
- Every pod in `monitoring` is `Running`, and Grafana answers on node port
  30300 (pinned in `infrastructure/monitoring.yaml`).

## Rollback / abort

- Part A changes nothing but the service's state; it can be repeated.
- Part B is not reversible once step 2 has run: the old datastore is gone.
  Nothing in it is needed, since Git holds every deployed object, so the abort
  is to repeat part B from step 3. Do not start step 2 without the packs
  copied (step 1) or a way to re-cook them.

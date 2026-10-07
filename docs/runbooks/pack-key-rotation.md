# Rotate the Pack Signing Key and Re-sign Packs

Replaces the Ed25519 key that signs an environment's asset packs and deploys
packs signed with the new one. What a pack is and how it is signed is ADR-0018
([runtime asset format](../adr/0018-runtime-asset-format.md)) and ADR-0031
([pack container](../adr/0031-pack-container-format.md)); who signs release
packs, and where, is [ENGINEERING.md's Release signing](../ENGINEERING.md#cicd).

## When to use

- The private key is lost, leaked, or suspected compromised.
- Moving an environment, or a release, onto a new key.

Not for the committed test key (`tests/fixtures/example-packs/test.key`), which
signs only the golden test packs and is never trusted outside the tests.

## How re-signing works

There is no command that re-signs an existing pack: `augusta-pack` signs as it
cooks. Re-signing is a fresh cook of the scenario with the new key, and the
client and server packs must come from that one cook run, since Join refuses a
client pack not cooked with the server pack (ADR-0019, ADR-0038). Each server
reads its pack from its own folder of the node's shared volume,
`/srv/augusta/asset-packs/<scenario>/<packVersion>/`, holding `server.pack` and
the `signing.pub` it is signed with
([`charts/augustad/values.yaml`](../../charts/augustad/values.yaml)). A new cook
has a new `packVersion`, so `augusta-publish` puts it in a new folder, and the
server is pointed at it through Git, which keeps the old folder in place as the
rollback.

## Where packs go on the node

Each server mounts its own version's folder read-only and reads `server.pack`
and `signing.pub` from it
([`deployment.yaml`](../../charts/augustad/templates/deployment.yaml)). Every
`packVersion` an environment's `servers` name
(`git grep -n packVersion -- clusters/`) needs, on the node:

```text
/srv/augusta/asset-packs/
└── <scenario>/
    └── <packVersion>/    # the server pack's BLAKE3 hash, first 12 hex characters
        ├── server.pack   # the server pack of one cook run
        └── signing.pub   # the 32-byte public key that cook was signed with,
                          # renamed to signing.pub whatever its name was locally
```

`augusta-publish` writes exactly this, and never over a folder that exists. The
volume is a `hostPath` of type `Directory`: until the folder is there, the pod
waits in `ContainerCreating` and its events name the missing path. A pack that
does not verify against `signing.pub` makes the server exit at startup, and the
pod crash-loops. The client pack never goes on the node.

## Prerequisites

- The pack environment, built with
  `tools\pack\scripts\bootstrap-windows.ps1 <AssetsRoot>` (Windows, ADR-0030).
  The commands below set `$AssetsRoot` to it.
- The authoring content the current packs were cooked from, under
  `<AssetsRoot>\authoring\` (only the example scenario is in the repository).
- For the release key: the offline location the release private key is kept in.
  The repository does not record it; it is never in the repository or a CI
  secret.
- SSH access to the k3s node (`<node>` below, as `ssh` names it), as a user that
  can `sudo` without a password. The repository does not record its address.
- A kubeconfig for the cluster, `kubectl`, and the `flux` CLI (see
  [Roll Back a Bad Deploy with Flux](flux-rollback.md#prerequisites)).
- A Windows client and server build to smoke-test the packs locally.

## Steps

1. Choose the new folder name, `<Id>`, unused on the node (for example the
   scenario name and a date, `firebase-2026-10`), and set it up in PowerShell:

    ```powershell
    $AssetsRoot = "<AssetsRoot>"
    $Id = "<Id>"
    ```

2. Generate the new keypair under a new prefix. `augusta-keygen` overwrites
   whatever is at the prefix, so never reuse `keys\signing` or an existing one:

    ```powershell
    & "$AssetsRoot\bin\augusta-keygen.exe" "$AssetsRoot\keys\$Id"
    ```

    This writes `keys\<Id>.pub` (32 bytes) and `keys\<Id>.key` (64 bytes). For
    the release key, do this on the offline machine that holds release keys, and
    move `<Id>.key` into its offline storage once step 3 has cooked.

3. Cook the scenario once, signed with the new key, into the new folder:

    ```powershell
    & "$AssetsRoot\bin\augusta-pack.exe" firebase `
      --signing-key "$AssetsRoot\keys\$Id.key" `
      --client-output-pack "$AssetsRoot\packs\$Id\client.pack" `
      --server-output-pack "$AssetsRoot\packs\$Id\server.pack"
    ```

    Replace `firebase` with the scenario the environment runs.

4. Verify both packs against the new public key, and check the old key no longer
   verifies them:

    ```powershell
    & "$AssetsRoot\bin\augusta-verify.exe" "$AssetsRoot\packs\$Id\server.pack" --public-key "$AssetsRoot\keys\$Id.pub"
    & "$AssetsRoot\bin\augusta-verify.exe" "$AssetsRoot\packs\$Id\client.pack" --public-key "$AssetsRoot\keys\$Id.pub"
    & "$AssetsRoot\bin\augusta-verify.exe" "$AssetsRoot\packs\$Id\server.pack" --public-key "$AssetsRoot\keys\signing.pub"
    ```

    The first two print `OK:`; the third must exit 1 (substitute the old key's
    path if it is not `keys\signing.pub`).

5. Smoke-test locally: run `augustad` and `augustac` with configs
   ([`config/augustad.example.yaml`](../../config/augustad.example.yaml),
   [`config/augustac.example.yaml`](../../config/augustac.example.yaml)) whose
   `content.pack` and `content.public_key` name the new packs and
   `keys\<Id>.pub`. Both log `event=pack_verified`, and the client is admitted
   at Join.

6. Publish the server pack and the new public key to the node. This first
   verifies both packs against the key, and that they come from one cook:

    ```powershell
    & "$AssetsRoot\bin\augusta-publish.exe" firebase --host <node> `
      --public-key "$AssetsRoot\keys\$Id.pub" `
      --client-pack "$AssetsRoot\packs\$Id\client.pack" `
      --server-pack "$AssetsRoot\packs\$Id\server.pack"
    ```

    Replace `firebase` with the scenario. It prints the folder it wrote and the
    `packVersion` to serve it with.

7. Point the scenario's server at the new version: on a `feature/*` branch off
   `develop`, set `spec.values.servers.<scenario>.packVersion` to the printed
   version, quoted, in
   [`clusters/onprem/apps/develop.yaml`](../../clusters/onprem/apps/develop.yaml)
   and/or [`staging.yaml`](../../clusters/onprem/apps/staging.yaml), commit
   (`chore(cluster): move <environment>'s <scenario> server to a new pack`), and
   merge it to `develop` through a pull request. Both files deploy from
   `develop`, staging's included. The change alters the server's volume, which
   restarts its pod and no other server's.

8. Make Flux apply it without waiting for its interval:

    ```sh
    flux reconcile kustomization apps -n flux-system --with-source
    ```

9. Give the client pack (`packs\<Id>\client.pack`) and `keys\<Id>.pub` to every
   client that joins this environment: a client with the old pack is refused at
   Join. The repository defines no distribution channel for client packs.

10. Retire the old key once no environment names a version signed with it
    (`git grep -n packVersion -- clusters/`): remove those folders from the
    node, and destroy or archive the old private key.

    ```sh
    ssh <node> "sudo rm -r /srv/augusta/asset-packs/<scenario>/<old-packVersion>"
    ```

## Verification

```sh
flux get kustomizations -n flux-system
kubectl -n develop rollout status deploy/augustad-<scenario> --timeout 10m   # or -n staging
kubectl -n develop logs deploy/augustad-<scenario> | grep 'event=pack_verified'
```

The pod mounts the new version's folder
(`kubectl -n develop get deploy augustad-<scenario> -o yaml | grep asset-packs`),
and keeps running: the server exits at startup on a pack that does not verify
against `signing.pub`. A client with the new `client.pack` and `<Id>.pub` logs
`event=pack_verified` and joins.

## Rollback / abort

- Before step 7, nothing is deployed: delete `packs\<Id>\`, `keys\<Id>.*` and
  the node's `/srv/augusta/asset-packs/<scenario>/<packVersion>/`.
- After step 7, revert the `packVersion` commit on `develop`
  ([Roll Back a Bad Deploy with Flux](flux-rollback.md), steps 4 to 6). The old
  folder is still on the node, so the pod restarts on the old pack. Do not start
  step 10 until the new packs have run cleanly.
- A leaked key cannot be rolled back to: if the rotation is because of a leak,
  fix forward instead.

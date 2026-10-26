# Cut a Release

Takes `develop` to a tagged release on `main` through a Git Flow
`release/vX.Y.Z` branch, publishes the GitHub Release, and merges the release
back into `develop`. The branching model, changelog and release workflow are
[ENGINEERING.md's Git Workflow](../ENGINEERING.md#git-workflow) and
[CI/CD](../ENGINEERING.md#cicd); what deploys a release to `staging` is ADR-0026
([CD strategy](../adr/0026-cd-strategy.md)) and ADR-0056
([signed revisions](../adr/0056-signed-revisions-for-flux.md)); what a release
is tested with is ADR-0013 ([testing](../adr/0013-testing-and-benchmarking.md)).

## When to use

- `develop` holds what the next release ships, and the release is decided.

A fix to a release already out is a `hotfix/vX.Y.Z` branch off `main` instead.
It follows the same steps with `hotfix/` for `release/`, except that step 2
branches off `main` (`git switch -c hotfix/vX.Y.Z origin/main`) and step 4 is
skipped: a branch cut from `main` already has everything `main` has.

## Pack format changes

The engine reads packs of one pack format version, `kFormatVersion` in
[`src/modules/assets/assets.cpp`](../../src/modules/assets/assets.cpp)
(ADR-0031), and a server exits at startup on a pack of any other. Each server in
[`clusters/onprem/apps/`](../../clusters/onprem/apps/) records the `packFormat`
its pack was cooked in beside its `packVersion`, and CI's `format` job, a
required check, fails a pull request after which any `packFormat` there differs
from `kFormatVersion`, or a `packVersion` has none
([`scripts/check-pack-formats.py`](../../scripts/check-pack-formats.py)).

A change to `kFormatVersion` therefore needs every scenario re-cooked in the new
format, for every environment, before it merges. A change that leaves the format
version alone needs no re-cook. In order:

1. On the `feature/*` branch that changes the format, cook each scenario
   `clusters/` names (`git grep -n packVersion -- clusters/`) with that branch's
   `augusta-pack`, once signed with `develop`'s key and once with the release
   key, and publish each cook to its environment
   ([Rotate the Pack Signing Key](pack-key-rotation.md), steps 3 to 6, with the
   environment's existing key in place of a new one). Publishing changes nothing
   that runs.
2. In the same pull request, set every server's `packVersion` and `packFormat`
   in `develop.yaml` and `staging.yaml` to what `augusta-publish` printed. The
   `format` job passes once every `packFormat` is the new version.
3. Merge it, then cut the release that ships it without waiting for more
   changes. Flux applies the values at once, ahead of the binaries that read
   them: until CI publishes the merge commit's chart (`develop`) and until this
   release's tag is pushed (`staging`), each environment's new pod refuses the
   new pack and its old pod keeps serving, with the `HelmRelease` not `Ready`.

A hotfix tagged in that window, off a `main` still in the old format, does not
reach `staging` either: its pod refuses the new-format packs `staging.yaml`
names. Ship the fix in the release that carries the format change instead, or
cut that release first and the hotfix off it.

## Prerequisites

- `gh`, authenticated, with permission to merge pull requests to `main` and
  `develop`. Both branches require a pull request, signed commits, and the
  `sanitizers`, `changes`, `format`, `client-x64-windows` and `server-x64-linux`
  checks. The repository allows merge commits only, and deletes a pull request's
  branch once it merges.
- Commit and tag signing configured locally (the `v*` tags are signed, annotated
  tags), and the repository's hooks active
  (`git config core.hooksPath .githooks`). The tag must be signed with an SSH
  signing key Flux trusts for releases (`trusted-signers-release` in
  [`trusted-signers.yaml`](../../clusters/onprem/flux-system/trusted-signers.yaml)),
  or `staging` never runs it.
- `bash` and `uvx` (uv) for
  [`scripts/changelog.sh`](../../scripts/changelog.sh), which runs git-cliff
  through `uvx` with [`cliff.toml`](../../cliff.toml): WSL, the dev container,
  or Git Bash on Windows.
- `podman`, `jq` and `cosign`, for step 8's wait on the image:
  `scripts/bootstrap.sh` installs `podman` and `jq`, and `cosign` is the
  toolchain's, the one CI signs with. Run `make toolchain`, then
  `source .toolchain/activate` in the shell that runs step 8. GHCR serves the
  image and its signatures without a login.
- Access to the cluster's `develop` environment, for the NFR-01 check in step 6.

## Steps

1. Choose the version, `X.Y.Z`, and check its tag is unused:

    ```sh
    git fetch origin --tags
    git tag --list 'vX.Y.Z'      # prints nothing
    ```

2. Branch off `develop`:

    ```sh
    git switch -c release/vX.Y.Z origin/develop
    ```

3. Bump the version in its three places, rewrite the fixture that names it, then
   commit:

    - `src/modules/version/version.cpp`: `EngineVersion()`'s release is
      `"X.Y.Z"`, which a Development build returns with `+dev` appended
      (ADR-0074). Join refuses a client of another engine version (ADR-0038).
    - `vcpkg.json`: `"version": "X.Y.Z"`.
    - `charts/augustad/Chart.yaml`: `version: X.Y.Z` and `appVersion: "X.Y.Z"`.

    Then rewrite the dissector's fixture, which names the engine version
    (`tests/fuzz/README.md`), with a Development build of the bump:

    ```sh
    cmake --build --preset linux --target augusta_protocol_decode_seeds_pcapng
    ```

    ```sh
    git commit -am "chore(release): bump the engine, manifest and chart to X.Y.Z"
    ```

4. If `main` has commits `develop` lacks (a hotfix not yet merged back), merge
   them now, so the pull request to `main` has no conflicts:

    ```sh
    git merge origin/main
    ```

    Only fixes for the release land on the branch from here on, each as its own
    Conventional Commit.

5. Cut the changelog section, last, and commit it. A `chore(release)` commit is
   left out of the changelog it cuts (`cliff.toml` skips `chore`):

    ```sh
    scripts/changelog.sh vX.Y.Z
    git diff CHANGELOG.md                         # review the new "## X.Y.Z - <date>" section
    scripts/changelog.sh vX.Y.Z --notes           # what the GitHub Release will say
    git commit -am "chore(release): cut the X.Y.Z changelog"
    ```

    If more fixes land on the branch afterwards, run
    `scripts/changelog.sh vX.Y.Z` again: a re-cut replaces the section. The
    release workflow refuses a tag whose version has no section.

6. Push and open the pull request to `main`, carrying the release's evidence:

    ```sh
    git push -u origin release/vX.Y.Z
    gh pr create --base main --title "chore(release): vX.Y.Z" --body "<evidence checklist>"
    ```

    Before merging, record in the pull request the check CI cannot run
    (`release/*` is never deployed, ADR-0026). That the environments' packs are
    of the format this branch's binaries read is the pull request's `format`
    job's ([Pack format changes](#pack-format-changes)):

    - NFR-01 on the cluster, eight players for a full Match in the `develop`
      environment (ADR-0013). That environment runs `develop`'s head, not this
      branch, so the measurement covers the release only while both carry the
      same code. Check that nothing but the step 3 version bump differs:

        ```sh
        git fetch origin
        git diff --stat origin/develop release/vX.Y.Z -- src/ charts/ Dockerfile vcpkg.json CMakeLists.txt cmake/ third_party
        ```

        If more differs (a fix made on this branch, or `develop` has moved on),
        the measurement does not count for the release: merge this pull request
        when its other checks pass, but do the back-merge (step 10) before
        tagging (step 8), measure NFR-01 once Flux has deployed that `develop`
        commit, and tag only after it passes.

7. Merge it once its checks pass, as a merge commit:

    ```sh
    gh pr merge <number> --merge
    ```

    This changes nothing that runs: `staging` follows the release tags, not
    `main`, and moves at step 8.

8. Tag `main`'s merge commit and push the tag, which starts the release workflow
   ([`.github/workflows/release.yml`](../../.github/workflows/release.yml)) and
   moves `staging` to it: Flux verifies the tag's signature, then upgrades
   `staging` to the chart and image of the tagged commit. Wait first until
   `main`'s CI has published that commit's image as the index of both
   architectures and signed it (its `container-index` job), so `staging` never
   runs a chart whose image is missing, or one the cluster's signature policy
   would refuse (ADR-0056, ADR-0057):

    ```sh
    git fetch origin
    git log -1 origin/main          # the "Merge pull request #<number> from .../release/vX.Y.Z" commit
    image=ghcr.io/sergioffpc/augustad:sha-$(git rev-parse --short=12 origin/main)
    both='[.manifests[]?.platform.architecture] | contains(["amd64", "arm64"])'
    signer=https://github.com/sergioffpc/augusta/.github/workflows/ci.yml@refs/heads/main
    until podman manifest inspect "$image" 2>/dev/null \
        | jq --exit-status "$both" >/dev/null \
      && cosign verify "$image" --certificate-identity "$signer" \
        --certificate-oidc-issuer https://token.actions.githubusercontent.com >/dev/null 2>&1; do
      sleep 60
    done
    git tag -s vX.Y.Z -m vX.Y.Z origin/main
    git push origin vX.Y.Z
    ```

    If `main`'s CI fails instead
    (`gh run list --workflow ci.yml --branch main`), no image is published and
    nothing is tagged: fix it on a `hotfix/*` branch off `main` as
    [Rollback / abort](#rollback--abort) describes, and tag its merge commit
    once its image is published.

9. Watch the workflow. Its `client-x64-windows`, `client-x64-linux` and `server`
   jobs rebuild and test the binaries as Shipping builds, with the shipping
   presets (ADR-0074), and `client-x64-windows` and `client-x64-linux` check the
   zip and the tarball start from an empty folder and print the tag's version
   exactly, so a `+dev` client fails the run (ADR-0058). Once the three build
   jobs pass, its `nightly` job runs every nightly job on the tagged commit
   (ADR-0013), the scan of the SBOMs just built among them. `publish` creates
   the GitHub Release only if all of them pass. Plan for the builds' time and
   then a night's on top, over an hour in all: the fuzzing alone takes 30
   minutes per target, the targets side by side. A failing nightly job here
   fails the run and opens no `nightly-failure` issue; the run is where it
   shows:

    ```sh
    gh run list --workflow release.yml --limit 1
    gh run watch <run-id>
    ```

10. Merge the release back into `develop`. The branch was deleted when step 7
    merged, so push it again from the local copy. Fast-forward it to `main`
    first: the tag is on `main`'s merge commit, which the branch alone never
    reaches, and `develop` must contain the tag (see Verification):

    ```sh
    git switch release/vX.Y.Z
    git fetch origin
    git merge --ff-only origin/main
    git push -u origin release/vX.Y.Z
    gh pr create --base develop --title "chore(release): merge vX.Y.Z back into develop" --body "<what reaches develop>"
    ```

    If it conflicts, merge `origin/develop` into the branch, resolve, push, and
    let the checks run again. Then:

    ```sh
    gh pr merge <number> --merge
    git switch develop && git pull
    git branch -d release/vX.Y.Z
    ```

## Verification

```sh
gh release view vX.Y.Z
gh release download vX.Y.Z --dir release-vX.Y.Z
gh attestation verify release-vX.Y.Z/augustac-windows-x64.zip --repo sergioffpc/augusta
gh attestation verify release-vX.Y.Z/augustac-linux-x64.tar.gz --repo sergioffpc/augusta
gh attestation verify release-vX.Y.Z/augustad-linux-x64 --repo sergioffpc/augusta
gh attestation verify release-vX.Y.Z/augustad-linux-arm64 --repo sergioffpc/augusta
gh attestation verify oci://ghcr.io/sergioffpc/augustad:sha-<12> --repo sergioffpc/augusta
podman manifest inspect ghcr.io/sergioffpc/augustad:sha-<12>  # amd64 + arm64
git log --oneline -1 origin/develop      # the back-merge
flux get sources git augusta-release -n flux-system   # vX.Y.Z, verified
flux get helmreleases -n flux-system     # augustad-staging at X.Y.Z+<12>
```

- The GitHub Release `vX.Y.Z` exists with `augustac-windows-x64.zip` and its
  `.pdb`, `augustac-linux-x64.tar.gz` and its `.debug`, `augustad-linux-x64`,
  `augustad-linux-arm64` and their `.debug` files, and the `.spdx.json` SBOM of
  each executable. Its notes are the changelog section, then the Linux client's
  glibc baseline (Ubuntu 26.04 or newer). The zip's `augustac.exe --version` and
  the tarball's `augustac/augustac --version` print `augustac X.Y.Z` from an
  empty folder: the release workflow checks both before it publishes.
- `sha-<12>` is an index with a `linux/amd64` and a `linux/arm64` manifest:
  `main`'s `container-index` job ran.
- The attestations verify. `<12>` is the first 12 characters of the tagged merge
  commit: the image a release runs is the one CI published for it on `main`.
- `develop` contains the release
  (`git merge-base --is-ancestor vX.Y.Z origin/develop` exits 0).
- Flux's `augusta-release` source is at `vX.Y.Z` and verified, and Flux has
  upgraded `staging` to the chart `X.Y.Z+<12>`
  ([Roll Back a Bad Deploy with Flux](flux-rollback.md#verification) has the
  checks).

## Rollback / abort

- **Before step 7:** close the pull request and delete the branch
  (`git push origin --delete release/vX.Y.Z`); nothing is published.
- **The release workflow fails after the tag is pushed:** no GitHub Release
  exists (`publish` needs every job, the nightly's included). A job that failed
  for its runner rather than the code, such as a benchmark on a noisy one, can
  be rerun alone (`gh run rerun <run-id> --failed`). Otherwise fix on a
  `hotfix/*` branch off `main` through a pull request to `main`, re-cutting the
  changelog there (`scripts/changelog.sh vX.Y.Z`) if the fix belongs in the
  notes, then move the tag to the new merge commit once its image is published,
  waiting as step 8 does:

    ```sh
    git tag -d vX.Y.Z
    git push origin :refs/tags/vX.Y.Z
    git fetch origin
    # step 8's wait for ghcr.io/sergioffpc/augustad:sha-<12 of origin/main>
    git tag -s vX.Y.Z -m vX.Y.Z origin/main
    git push origin vX.Y.Z
    ```

    If a Release was created all the same, `gh release delete vX.Y.Z --yes`
    first. Merge the hotfix back into `develop` as in step 10. `staging` ran the
    tag from the moment it was pushed, and follows it to the new merge commit.

- **The published release is bad:** a published tag is never moved or reused.
  Restore `staging` first by pinning it to the previous good tag
  ([Roll back staging](flux-rollback.md#roll-back-staging)), then ship
  `X.Y.(Z+1)` as a hotfix (see [When to use](#when-to-use)), and remove the pin
  once its tag is pushed.

# Cut a Release

Takes `develop` to a tagged release on `main` through a Git Flow
`release/vX.Y.Z` branch, publishes the GitHub Release, and merges the release
back into `develop`. The branching model, changelog and release workflow are
[ENGINEERING.md's Git Workflow](../ENGINEERING.md#git-workflow) and
[CI/CD](../ENGINEERING.md#cicd); what deploys `main` is ADR-0026
([CD strategy](../adr/0026-cd-strategy.md)); what a release is tested with is
ADR-0013 ([testing](../adr/0013-testing-and-benchmarking.md)).

## When to use

- `develop` holds what the next release ships, and the release is decided.

A fix to a release already out is a `hotfix/vX.Y.Z` branch off `main` instead.
It follows the same steps with `hotfix/` for `release/`, except that step 2
branches off `main` (`git switch -c hotfix/vX.Y.Z origin/main`) and step 4 is
skipped: a branch cut from `main` already has everything `main` has.

## Prerequisites

- `gh`, authenticated, with permission to merge pull requests to `main` and
  `develop`. Both branches require a pull request, signed commits, and the
  `sanitizers`, `changes`, `format`, `client` and `server` checks. The
  repository allows merge commits only, and deletes a pull request's branch
  once it merges.
- Commit and tag signing configured locally (the `v*` tags are signed,
  annotated tags), and the repository's hooks active
  (`git config core.hooksPath .githooks`).
- `bash` and `uvx` (uv) for [`scripts/changelog.sh`](../../scripts/changelog.sh),
  which runs git-cliff through `uvx` with [`cliff.toml`](../../cliff.toml):
  WSL, the dev container, or Git Bash on Windows.
- The release packs, cooked and signed with the release key on the
  developer's machine ([Release signing](../ENGINEERING.md#cicd),
  [Rotate the Pack Signing Key](pack-key-rotation.md)), for the pre-merge check
  in step 6.
- Access to the cluster's `develop` environment, for the NFR-01 check in
  step 6.

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

3. Bump the version in its three places, then commit:

    - `src/modules/version/version.cpp`: `EngineVersion()` returns `"X.Y.Z"`.
      Join refuses a client of another engine version (ADR-0038).
    - `vcpkg.json`: `"version": "X.Y.Z"`.
    - `charts/augustad/Chart.yaml`: `version: X.Y.Z` and `appVersion: "X.Y.Z"`.

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

    If more fixes land on the branch afterwards, run `scripts/changelog.sh vX.Y.Z`
    again: a re-cut replaces the section. The release workflow refuses
    a tag whose version has no section.

6. Push and open the pull request to `main`, carrying the release's evidence:

    ```sh
    git push -u origin release/vX.Y.Z
    gh pr create --base main --title "chore(release): vX.Y.Z" --body "<evidence checklist>"
    ```

    Before merging, record in the pull request the checks CI cannot run
    (`release/*` is never deployed, ADR-0026):

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

    - The release packs load in this branch's binaries: client and server log
      `event=pack_verified`, and the client is admitted at Join (ADR-0018).

7. Merge it once its checks pass, as a merge commit:

    ```sh
    gh pr merge <number> --merge
    ```

8. Tag `main`'s merge commit and push the tag, which starts the release
   workflow ([`.github/workflows/release.yml`](../../.github/workflows/release.yml)):

    ```sh
    git fetch origin
    git log -1 origin/main          # the "Merge pull request #<number> from .../release/vX.Y.Z" commit
    git tag -s vX.Y.Z -m vX.Y.Z origin/main
    git push origin vX.Y.Z
    ```

9. Watch the workflow. Its `client`, `server` and `tools` jobs rebuild and test
   the Release binaries; `publish` creates the GitHub Release only if all three
   pass:

    ```sh
    gh run list --workflow release.yml --limit 1
    gh run watch <run-id>
    ```

10. Merge the release back into `develop`. The branch was deleted when step 7
    merged, so push it again from the local copy, which is at the same commit:

    ```sh
    git switch release/vX.Y.Z
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
gh attestation verify release-vX.Y.Z/augustac-windows-x64.exe --repo sergioffpc/augusta
gh attestation verify release-vX.Y.Z/augustad-linux-x64 --repo sergioffpc/augusta
gh attestation verify oci://ghcr.io/sergioffpc/augustad:sha-<12> --repo sergioffpc/augusta
git log --oneline -1 origin/develop      # the back-merge
flux get helmreleases -n flux-system     # augustad-staging at X.Y.Z+<12>
```

- The GitHub Release `vX.Y.Z` exists with `augustac-windows-x64.exe`,
  `augustad-linux-x64` and their `.spdx.json` SBOMs, and its notes are the
  changelog section.
- The attestations verify. `<12>` is the first 12 characters of the tagged
  merge commit: the image a release runs is the one CI published for it on
  `main`.
- `develop` contains the release (`git merge-base --is-ancestor vX.Y.Z origin/develop`
  exits 0).
- Flux has upgraded `staging` to the chart `X.Y.Z+<12>`
  ([Roll Back a Bad Deploy with Flux](flux-rollback.md#verification) has the
  checks).

## Rollback / abort

- **Before step 7:** close the pull request and delete the branch
  (`git push origin --delete release/vX.Y.Z`); nothing is published.
- **The release workflow fails after the tag is pushed:** no GitHub Release
  exists (`publish` needs every job). Fix on a `hotfix/*` branch off `main`
  through a pull request to `main`, re-cutting the changelog there
  (`scripts/changelog.sh vX.Y.Z`) if the fix belongs in the notes, then move
  the tag to the new merge commit:

    ```sh
    git tag -d vX.Y.Z
    git push origin :refs/tags/vX.Y.Z
    git fetch origin
    git tag -s vX.Y.Z -m vX.Y.Z origin/main
    git push origin vX.Y.Z
    ```

    If a Release was created all the same, `gh release delete vX.Y.Z --yes`
    first. Merge the hotfix back into `develop` as in step 10.

- **The published release is bad:** a published tag is never moved or
  reused. Restore `staging` first by pinning it to the previous good image
  ([Roll back staging](flux-rollback.md#roll-back-staging)), then ship
  `X.Y.(Z+1)` as a hotfix (see [When to use](#when-to-use)), and remove the pin
  once it is on `main`.

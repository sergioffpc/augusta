# Engineering Practices — FPS Simulator Engine

Covers CI/CD, code quality, design philosophy, observability, and performance.
Complements [ARCHITECTURE.md](./ARCHITECTURE.md) (system design) and
[ROADMAP.md](./ROADMAP.md) (milestones).

## Contents

<!-- pyml disable md051 -->

- [Design Philosophy](#design-philosophy)
- [CI/CD](#cicd)
- [Git Workflow](#git-workflow)
- [Deployment & CD](#deployment--cd)
- [Developer Environment](#developer-environment)
- [Code Quality](#code-quality)
- [Observability](#observability)
- [Performance](#performance)

<!-- pyml enable md051 -->

## Design Philosophy

A short set of principles this project holds itself to, distilled from the
decisions already made in ARCHITECTURE.md:

- **Data-oriented, ECS-first.** Simulation state lives in Flecs worlds, not
  scattered object graphs.
- **Mechanism vs. policy vs. data.** Engine mechanism (C++), game policy
  (sandboxed Lua), and tunable values (config/data) are three distinct
  categories, kept apart so each can change independently.
- **No I/O inside ECS worlds.** Worlds are pure state transformations; device
  input, networking, rendering, and audio live at the boundaries.
- **Determinism is not assumed, it's engineered around.** PhysX doesn't
  guarantee cross-platform determinism (ADR-0004) — the architecture corrects
  for reality (restoring the server's state and replaying from it) instead of
  pretending otherwise.
- **The server is the only source of truth.** Nothing from a client is trusted
  until validated (US-15).
- **Content is signed and verified, not just loaded.** Integrity is structural
  (ADR-0018), not an afterthought.
- **No premature optimization.** Profile first (NVTX ranges in Nsight Systems),
  then optimize; don't build custom allocators or job systems speculatively
  (ADR-0005, ADR-0007 risk notes).
- **Recoverable failures are values, not control flow.** `std::expected` for
  expected failure modes; exceptions only for truly unrecoverable startup
  errors; never exceptions in the per-tick hot path.
- **Prefer proven tools over reinventing them — except where reinventing is the
  point.** Ballistics, the ECS integration, and the network protocol are
  hand-built because that's the learning goal (VISION.md); rendering, physics,
  and transport are not.

## CI/CD

- **Provider:** GitHub Actions — native Windows and Linux runners match the
  client/server platform split exactly.
- **Job names:** a job that builds a deliverable (the client, the server, the
  server image) for one platform ends in that platform's vcpkg triplet,
  `<name>-<arch>-<os>`: `client-x64-windows`, `client-x64-linux`,
  `server-x64-linux`, `container-publish-arm64-linux`. A matrix job names each
  of its instances that way. A job that builds no single platform's deliverable
  (`format`, `changes`, `container-index`) has no suffix.
- **Trigger:** `push` to `main`/`develop`, and `pull_request` targeting either;
  a separate nightly workflow runs on `develop` (ADR-0013). A `changes` job
  diffs against the base commit first and skips build/test/lint entirely when
  nothing under `src/`, `tests/`, `tools/agent/` (the C++ tools, built and
  tested with the runtime), `tools/composer/examples/` (the example scenario a
  test loads), `tools/pack/native/` (formatted by the `format` job, though CI
  doesn't build it), `cmake/`, `config/` (the example configs a test loads),
  `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, the `third_party`
  submodule pointer, `.clang-format`/`.clang-tidy`, or the workflow file itself
  or the composite actions it shares (`.github/actions/`) changed (a docs-only
  PR shouldn't pay for a full build). `concurrency` cancels a still-running run
  for the same PR when a new push arrives, so superseded runs don't keep burning
  minutes. A newer push to `main`/`develop` never cancels a run there: Flux
  deploys every commit on those branches, and each commit's `sha-<commit>` image
  is published only by that commit's run. Those runs overlap, so only the
  branch's newest commit moves the branch tag.
- **Pipeline stages:**
    1. `clang-format` check, alone in its own fast job — gates everything below
       (`needs:`), so a formatting slip fails in seconds instead of after a full
       Windows + Linux + sanitizers build
    2. Build + test the client on a Windows runner (`clang-cl` and `lld-link`,
       ADR-0008), with the server's development-only Windows x64 build (NFR-04).
       Plus `clang-tidy` with the full profile on the client, the Renderer and
       their tests, which the Linux run below builds without
    3. Build + test the server on a Linux runner (clang, ADR-0008), on Linux
       x86-64, one of its two production platforms: Linux arm64 is built only
       for `main`'s image and the releases (NFR-04). Plus `clang-tidy` (Google
       style checks profile). It, the sanitizers job and the Windows job above
       run the tests one per core of the runner (`ctest --parallel "$(nproc)"`,
       `"$NUMBER_OF_PROCESSORS"` on Windows), so they follow the runner's size:
       each test listens on a port of its own (`:0`) and names its temporary
       files after itself, so they don't contend. The presets themselves stay
       serial, as do the nightly and release runs.
    4. ASan + UBSan test build and a short fuzzing run per target (both Linux
       only), only for `pull_request` runs — skipped on the `push` that lands
       after merge, since the PR already validated it
    5. Compile with a strict warning set, treated as errors: clang's
       `-Wall -Wextra -Werror` on both platforms (`/W4 /WX` to `clang-cl`)
    6. the `tools` job, when `tools/` changed: on a Windows runner, build the
       asset cooker's native module and run its pytest suite, which also
       requires that cooking the example scenario still gives the golden packs
       in `tests/fixtures/example-packs/` byte for byte; the C++ tests in 2 and
       3 load those same packs — the contract between the Python writer and the
       C++ reader of the pack format (ADR-0013). The golden packs are signed
       with a committed test key; the real release private key never touches CI
    7. The server image, in two jobs. `container-verify` needs only `changes`,
       so it runs alongside the server build: Helm and Dockerfile lint, then the
       image built into the runner's Docker and scanned by trivy. It runs on
       every push, and on a pull request when the image or chart changed; on a
       push it also builds the image's `debuginfo` stage, and hands both to the
       run as artifacts. `container-publish-x64-linux`, on a push only, waits
       for it, the server-x64-linux job and `confinement`, then pushes that very
       image (it builds none) with its `-debuginfo` image, signature and
       attestations - so a published image is always the one that was scanned
       and confined, of a commit whose tests passed. On a push to `main` alone,
       whose image is the one a release runs, `container-publish-arm64-linux`
       builds, scans and pushes the arm64 image too, natively on an arm64
       runner, as `sha-<12>-arm64` with its own debug info and attestations, and
       `container-index` then makes `sha-<12>`, `main` and `sha-<12>-debuginfo`
       the index of both architectures (NFR-04). On a push to `develop` alone,
       `chart-publish` then pushes the chart, versioned `<chart version>+<12>`,
       to `ghcr.io/sergioffpc/charts/augustad`, signs it, and tags it `develop`:
       what the `develop` environment runs (ADR-0056)
    - Dependency restore: `vcpkg install` (manifest mode) before the build step,
      both runners. Binary cache via a GitHub Packages NuGet feed on Windows
      (vcpkg's native GitHub-Actions-cache backend was removed upstream in
      2026). On Linux, where nuget.exe runs under Mono and fails certificate
      checks, it is a files cache in the Actions cache, one entry for every
      Linux job, saved only when a job built a package it didn't restore. A save
      keeps only the packages its job installed, so a job installing ports no
      other does keeps an entry of its own: the `tools` job's Linux leg, for the
      `tools/pack/native` manifest, and each job turning on a manifest feature
      the others don't (`client-x64-linux` the `client` feature,
      `server-x64-linux` the Agents' `agent` feature). vcpkg builds its ports
      through the overlay `x64-linux` triplet with the project's LLVM toolchain
      (ADR-0008), which no preset changes. So the entry's packages are the same
      whatever preset a job configures, and the server image's dependency layer,
      built on the same Ubuntu, restores them too: the container jobs mount the
      entry read-only into its `vcpkg install`, and it builds from source only
      the packages the entry lacks.
    - Compiler cache: sccache on its local disk backend, never its
      GitHub-Actions-cache one, whose one entry per object exhausts the cache's
      request quota. Each job carries the directory between runs as one entry of
      its own per ref (`.github/actions/restore-sccache-cache`,
      `save-sccache-cache`), saved after a successful build only when it
      changed, holding only the objects that build wrote or read, and replacing
      the ref's entry before it. A release's jobs restore main's entries and
      save none. An Actions cache outage only makes the build cold.
    - clang-tidy results: both C++ jobs' Lint steps run clang-tidy through
      ctcache, one translation unit per invocation, and skip a unit that passed
      with the same preprocessed source, `#include` lines, effective
      configuration and arguments. A unit with findings is never cached, so it
      fails every run until fixed; a header, flag or `.clang-tidy` change
      re-lints exactly the units it reaches. Not linting only a pull request's
      changed files: that misses the units a changed header or configuration
      reaches. Results live in a directory per clang-tidy version, which
      ctcache's key leaves out, and each job carries them between runs as the
      compiler cache is carried (`.github/actions/restore-ctcache-cache`,
      `save-ctcache-cache`), saved after Lint whether it passed or failed.
    - The Actions cache (10 GB per repository, least recently used evicted
      first) holds only what a pull request restores from `develop`: the vcpkg
      binaries, sccache objects and clang-tidy results. The server image's
      Docker layers live in GHCR (`augustad:buildcache`) instead: at several GB
      they would evict the rest, and every pull request would rebuild its
      dependencies from source. Only a push's `container-verify` writes them; a
      pull request reads them, and a fork's reads nothing.
    - An Actions cache entry is kept only on a ref a later run restores from
      (its own, a pull request's base, or `main`). A release restores the caches
      but saves none: its tag's entries would serve only a rerun of it, which
      restores `main`'s instead. A pull request's entries are deleted when it
      closes, and a branch's when the branch is deleted. CodeQL's TRAP cache is
      saved by `main` alone, an entry per commit, and only the newest is kept.

- **Nightly** (on `develop`): long fuzzing runs, TSan, property-based tests at a
  high case count, a `llvm-cov` coverage report, the Linux client's build and
  tests, again under ASan and UBSan (`client-x64-linux`: no pull request builds
  it), and the hot-path micro-benchmarks, whose history is kept on the
  `benchmarks` branch (started by the first night if absent) and charted on the
  documentation site; a failure, a benchmark more than twice as slow as the
  night before among them, opens or updates a `nightly-failure` issue
  (ADR-0013).
- **Not in CI:** profiling (NVTX with Nsight Systems/Graphics, interactive
  tools, not CI checks), and NFR-01's tick rate under load (checked by hand on
  the cluster before a release, ADR-0013).
- **Releases:** a separate workflow, triggered by `v*` tags, builds
  Release-config client/server binaries, runs the tests and the asset pipeline
  check against them, runs every nightly job on the tagged commit, and attaches
  the binaries to a GitHub Release only once all of it passes, its notes
  generated from the commits (Git Workflow below) — not run on every push, so
  cutting a release is a deliberate tag rather than automatic (ADR-0013,
  ADR-0026). Started by hand on a branch, the same workflow is a dry run that
  publishes nothing: how a change to it, or to the nightly jobs, is tried before
  it merges (`gh workflow run release.yml --ref <branch>`). The client ships as
  `augustac-windows-x64.zip`, its whole runtime folder, never as a lone `.exe`:
  some of its dependencies ship only as DLLs, so the client cannot be linked
  statically, and it carries the MSVC runtime so it needs no redistributable. A
  release is published only once its unzipped client runs `augustac --version`
  and prints the tag's version (`scripts/stage-client.sh`, ADR-0058). That
  catches a DLL it imports that is missing, not one loaded only once it runs.
  The Linux client ships as `augustac-linux-x64.tar.gz`, one `augustac/` folder
  checked the same way, and for linking nothing from the system beyond an
  allowlist; it runs on the glibc of Ubuntu 26.04 or newer (ADR-0058).
- **Documentation site:** the `docs` workflow builds the site (MkDocs Material
  for `docs/`, `README.md` and `GLOSSARY.md`, Doxygen for the C++ API) on every
  pull request, and publishes `main`'s on GitHub Pages (ADR-0046). `make docs`
  builds it locally into `build/docs-site/`, with the `uv` and Doxygen both
  bootstraps install.
- **Release signing:** release packs are signed by the developer, on the
  developer's machine, never by a workflow. The release Ed25519 keypair is
  generated offline with `augusta-keygen`, as `staging.key`/`staging.pub`: it is
  `staging`'s key, as `develop.key`/`develop.pub` is `develop`'s, and a
  `staging` server refuses a pack that does not verify against the `staging.pub`
  published beside it. It is distinct from the committed test key and from any
  development key. Its private key is kept off the repo and out of every CI
  secret. The client and server packs of a release come from one cook run signed
  with it, since Join refuses a client pack not cooked with the server pack
  (ADR-0019, ADR-0038). Only the public key, `signing.pub`, travels with the
  packs, named by each executable's config (ADR-0034).
- **Provenance and SBOMs:** what packs get from their signature, the release
  executables and every published server image get from GitHub artifact
  attestations, signed keylessly (Sigstore) by the workflow that built them:
  SLSA build provenance, naming the commit and the workflow run, and an SPDX
  SBOM. Each is attested where it is published, so the image is attested by CI
  on the push that publishes it (`.github/actions/sign-and-attest-server-image`,
  for either architecture), and the image a release runs (`sha-<12>` of its
  tag's commit) needs nothing more from the release. On `main` each
  architecture's image is attested, and the index of both gets its own
  provenance; a release attests `augustad-linux-x64` and `augustad-linux-arm64`
  alike. An executable's SBOM, attached to the Release, lists the vcpkg ports it
  was built with, taken from vcpkg's own per-port SPDX documents
  (`scripts/vcpkg-sbom.py`), plus the submodules it uses, by commit: syft finds
  nothing in a statically linked C++ binary. The client's also lists the MSVC
  runtime its zip ships beside it (ADR-0058). The image's SBOM is syft's, of its
  Ubuntu packages. Verified with:

    ```sh
    gh attestation verify augustac-windows-x64.zip --repo sergioffpc/augusta
    gh attestation verify augustad-linux-x64 --repo sergioffpc/augusta
    gh attestation verify oci://ghcr.io/sergioffpc/augustad:sha-<12> \
      --repo sergioffpc/augusta
    # The SBOM attestation, rather than the provenance:
    gh attestation verify augustad-linux-x64 --repo sergioffpc/augusta \
      --predicate-type https://spdx.dev/Document/v2.3
    ```

- **Image signatures:** every server image CI publishes on `develop` or `main`
  is also signed with cosign, keylessly, by digest: each architecture's image,
  and on `main` the index of both (`.github/actions/sign-artifact`). The job
  verifies the signature at once against the identity the cluster admits, this
  repository's `ci.yml` on `develop` or `main`, and fails if it does not verify.
  In the cluster, Kyverno's `verify-image-signatures` policy checks the same
  identity for `augustad`, and the publishers' for Flux's and Kyverno's images
  (ADR-0057). cosign also counts the image's attestations, which the same
  workflow signs, so the signature proper is the entry whose type is
  `https://sigstore.dev/cosign/sign/v1`:

    ```sh
    cosign verify ghcr.io/sergioffpc/augustad:sha-<12> \
      --certificate-identity-regexp \
        '^https://github\.com/sergioffpc/augusta/\.github/workflows/ci\.yml@refs/heads/(develop|main)$' \
      --certificate-oidc-issuer https://token.actions.githubusercontent.com \
      | jq '.[].critical.type'
    ```

- **Vulnerability scanning:** the server image is scanned by trivy as CI builds
  it, and is published only if clean. A release's executables are scanned by
  grype, from their SBOMs (`nightly-jobs.yml`'s `sbom-scan`), before the release
  publishes them, and the latest release's again every night, so a CVE published
  after the release still fails a run. No vulnerability database knows vcpkg's
  purls, so `scripts/sbom-cpes.py` first adds each port's NVD CPE, and fails on
  a port that has neither a CPE nor a written reason for having none. Both fail
  on a high or critical CVE; the image only on one with a fix available, the
  executables on any, since the NVD gives most of its ranges with no fixed
  version.
- **Package retention:** GitHub Packages keeps every version CI pushes, so the
  `prune-packages` workflow (`scripts/prune-packages.py`) deletes the ones
  nothing needs. `augustad` keeps `buildcache`, `buildcache-arm64`, `develop`,
  `main`, the images of every release, of `main`'s newest commit and of the
  newest `develop` commits (20 by default), what `develop` and `staging` run,
  every tag or digest `develop` or `main` names, and whatever was pushed in the
  day before its newest version, which a CI run may still be using; with each of
  them, its platform manifests, debug info, signatures and attestations.
  `charts/augustad` keeps `develop` and the charts of those releases and
  commits, what `develop` can be rolled back to
  ([flux-rollback.md](runbooks/flux-rollback.md)). Each vcpkg NuGet package
  keeps its newest versions (2 by default): a miss rebuilds the port. If what CI
  or the cluster runs now is missing, or a manifest cannot be read, the run
  fails before anything is deleted. Scheduled runs print the plan alone;
  deleting it is a dispatch with `apply` set, after reading that plan.

## Git Workflow

- **Branching model:** Git Flow — `main` (production/release) + `develop`
  (integration), with `feature/*`, `release/*`, `hotfix/*` branches.
- **Tags/releases:** created only when there's an actual release to make (e.g.,
  v1.0.0) — ROADMAP.md milestones (M0–M18) are internal checkpoints, not tagged
  releases.
- **Pull requests:** used even solo — `feature/*` → `develop`,
  `release/*`/`hotfix/*` → `main`, and the same `release/*`/`hotfix/*` branch
  back into `develop`, go through a PR so CI gates the merge; no formal review
  requirement, self-merge once CI passes. Cutting a release step by step is
  [docs/runbooks/cut-release.md](runbooks/cut-release.md).
- **Commit messages:** Conventional Commits, enforced via the local `commit-msg`
  hook (see Code Quality below).
- **Changelog and release notes:** generated from the Conventional Commits by
  git-cliff, never written by hand; `cliff.toml` decides which types are listed
  (`chore`, `ci` and `style` are not). A release's `CHANGELOG.md` section is cut
  with `scripts/changelog.sh` on its `release/*` or `hotfix/*` branch, in a
  `chore(release)` commit so the cut leaves itself out, and the release workflow
  publishes that section as the GitHub Release's notes, refusing a tag that has
  none.

## Deployment & CD

Covers `main` and `develop` only — `feature/*`, `hotfix/*`, and `release/*`
branches are not deployed to k3s at all (see ADR-0026 for why, and why there's
no self-hosted GitHub Actions runner in this pipeline).

- **Infrastructure:** self-hosted k3s, single node, on the developer's own
  hardware. No cloud provider involved.
- **Isolation:** two fixed, long-lived Kubernetes namespaces — `staging` (the
  latest signed `v*` release tag, ADR-0056) and `develop` (tracks `develop`). No
  per-branch/ephemeral namespaces.
- **Container images:** built in CI, pushed to GitHub Container Registry (GHCR).
  The server image is `scratch` with an Ubuntu 26.04 root cut by chisel: the
  libraries `augustad` links, `tini`, and no shell (ADR-0054). A running server
  is inspected from an ephemeral container, `kubectl debug --target=augustad`,
  not `kubectl exec`.
- **Server exposure:** plain Kubernetes `Service` (`NodePort`) — no Agones.
  Agones solves fleet-scale dynamic allocation, which this project doesn't need
  (one server instance per scenario per environment, each fixed in Git,
  ADR-0026); revisit only if matchmaking/dynamic multi-server allocation is ever
  needed. Each server pins its node port, so LAN clients keep one address, from
  its environment's own range so `develop` and `staging` never collide on the
  shared node: `develop` 30700-30799, `staging` 30800-30899. The chart refuses a
  server without a node port in its range.
- **CD mechanism:** pull-based via Flux, running inside the k3s cluster and
  reconciling each environment's `HelmRelease` from Git (and `develop`'s chart
  from what CI signed on GHCR) — nothing outside the cluster needs inbound
  access to the LAN, and no external PR can trigger execution on the cluster
  host, since there's no CI runner in this path at all (see ADR-0026).
- **Signed revisions only:** Flux verifies every revision before reconciling it
  (ADR-0056), against public keys kept in Git
  ([`trusted-signers.yaml`](../clusters/onprem/flux-system/trusted-signers.yaml)).
  `develop`'s HEAD, which the `flux-system` source tracks too, must be signed by
  one of the maintainer's SSH signing keys or GitHub's `web-flow` key (a pull
  request merged on GitHub). `develop` runs the chart CI published and signed,
  keyless, for the branch's newest commit once its image was published, with the
  values of that HEAD; a commit whose CI fails leaves it on the previous chart.
  `staging` runs the newest `vX.Y.Z` tag by semver, verified against the
  maintainer's keys alone: a merge to `main` changes nothing that runs, and
  `staging` moves when the release's signed tag is pushed, once its image is
  ([Cut a Release](runbooks/cut-release.md)). A revision that fails verification
  leaves its environment on the last verified one.
- **Access:** LAN-only — no public exposure, no VPN/tunnel needed for now.
- **Asset packs:** built and signed manually, separately from the CD pipeline
  (see ADR-0018, CI/CD above). Packs are versioned independently of code deploys
  and can be shared across multiple server instances/versions. Each
  environment's are stored in its own folder of the k3s node,
  `/srv/augusta/asset-packs/<environment>`, its `assetPacks.hostPath`, which
  `augusta-publish --environment <environment>` fills after signing (ADR-0026):
  `<scenario>/<packVersion>/` in it holds a scenario's `server.apak` and the
  `signing.pub` key it is signed with, `<packVersion>` being the first 12 hex
  characters of the server pack's BLAKE3 hash. Each environment signs with its
  own key, `develop`'s or `staging`'s (Release signing above). The hash does not
  cover the signature, so one cook signed for both is one `packVersion` in two
  folders. A published folder is never rewritten. Each environment's Helm values
  list its servers, one per scenario, each naming the `packVersion` it runs and,
  beside it, the `packFormat` that pack was cooked in, both as `augusta-publish`
  prints them. The engine refuses a pack of any format version but its own
  (ADR-0031), so CI's `format` job
  ([`scripts/check-pack-formats.py`](../scripts/check-pack-formats.py)) fails a
  change after which any `packFormat` in `clusters/` differs from the engine's,
  or a `packVersion` has none: a change to the pack format moves every
  environment to packs re-cooked in it, in the same pull request
  ([cut-release](runbooks/cut-release.md#pack-format-changes)). The chart makes,
  for each server, a `local` PersistentVolume on that folder alone, read-only
  and pinned to the node, and the claim bound to it (ADR-0057), both named by
  the version: `<namespace>-augustad-<scenario>-pack-<packVersion>` and
  `augustad-<scenario>-pack-<packVersion>`. A new version is a new volume and
  claim, and the previous ones stay, so reverting the line goes back to them;
  Helm never deletes either, nor Kubernetes the folder
  ([Volumes](runbooks/k3s-node-recovery.md#volumes)). A server whose version is
  not published waits in `ContainerCreating`, its `FailedMount` event naming the
  missing folder. The chart writes each server's `augustad.yaml` from its
  values.
- **Crash dumps:** a crashing server logs its stack and leaves a kernel core
  dump. The k3s node must run `systemd-coredump` as its core handler, which
  keeps the dump (`coredumpctl`). A cluster core is read with the debug info CI
  publishes beside each image as its `sha-<12>-debuginfo` tag (on `main`, an
  index whose manifest for the node's architecture is pulled); a release's
  `augustad-linux-<arch>.debug` reads only that release binary, which no image
  runs (ADR-0047).
- **Client symbols:** each release attaches `augustac-windows-x64.pdb`, the PDB
  of the `augustac.exe` in the `augustac-windows-x64.zip` beside it, which reads
  a crash of that client or its Aftermath `.nv-gpudmp`. Rename it
  `augustac.pdb`, the name the `.exe`'s debug directory gives it; the two match
  by GUID and age (`dumpbin /headers`, ADR-0055). The Linux client's is
  `augustac-linux-x64.debug`, matched by build ID, as `augustad`'s is.
- **Server pod:** runs as UID/GID 65532 in a user namespace of its own, with no
  privilege escalation, every capability dropped, augustad's own seccomp profile
  and a read-only root filesystem; augustad writes only to its capture volume,
  if any ([confinement](runbooks/k3s-node-recovery.md#the-servers-confinement)).
  None of it changes core dumps, which the kernel pipes to the node's
  `systemd-coredump`, or `kubectl debug --target=augustad`: its ephemeral
  container has its own root filesystem and `securityContext`, and reads the
  server's files through `/proc/<pid>/root/` as the same user. The node has that
  user too, `augusta` (ADR-0054): the asset packs are `augusta-publish:augusta`,
  readable by its group alone, and the capture volume is its own
  ([The server's user](runbooks/k3s-node-recovery.md#the-servers-user)).
- **Match captures:** an environment turns them on in its Helm values
  (`captures.hostPath`, `develop` only), and each of its servers then captures
  every Match (ADR-0050) into `<hostPath>/<namespace>/<scenario>/` on the node,
  a folder of its own, since capture names are unique only within one server.
  Each server mounts `<hostPath>` read-write as a `local` PersistentVolume of
  its own, `<namespace>-augustad-<scenario>-captures`, made and kept as the
  packs' are. `<hostPath>` is `/srv/augusta/captures`, owned by `augusta`, a 4
  GiB ext4 image loop-mounted there, so a full one stops captures and never
  fills the node. Its size holds every capturing server's retention cap
  (`captures.retention.maxMiB`, #461) plus slack. Node setup:
  [The capture filesystem](runbooks/k3s-node-recovery.md#the-capture-filesystem).
- **Admission policies:** Kyverno runs in the cluster, installed by Flux with
  its infrastructure, and the policies in
  [`clusters/onprem/policies/`](../clusters/onprem/policies/kustomization.yaml)
  are enforced there, failing closed: while Kyverno cannot answer, every request
  it judges is refused, except in `kube-system` and `kyverno`, which its
  webhooks skip so that it can always start (ADR-0057). CI's `manifests` job
  runs the same policy files with the Kyverno CLI over each environment's
  rendered chart, each Flux Kustomization path and each infrastructure
  `HelmRelease` rendered with its values, so a change the cluster would refuse
  fails its pull request; it also requires each policy to refuse its negative
  fixtures in `fixtures/<policy>/`. Adding a policy is its file, its entry in
  the Kustomization and its fixtures, with no change to CI.
- **Pod Security:** `develop` and `staging` enforce the `restricted` Pod
  Security Standard, which the chart's pods meet: they mount the node's folders
  from PersistentVolumes, never as `hostPath` volumes, which a
  `host-path-volumes` policy refuses outside the `privileged` namespaces
  (ADR-0057).
- **One credential, not in Git:** the cluster's only credential is
  Alertmanager's Telegram bot token, in a Secret the operator makes by hand
  (`docs/runbooks/alert-notifications.md`). A `secret-allowlist` policy refuses
  any Secret but that one, Flux's trusted public keys, Helm's release records
  and those the charts make themselves, each listed by name (ADR-0057).
- **Images by digest:** every image runs as `<image>:<tag>@sha256:<digest>`, so
  a tag pushed again never changes what runs, except those whose signatures are
  verified instead (`augustad`'s and Flux's) and `kube-system`'s, which come
  with k3s's version (ADR-0057). The infrastructure's `HelmRelease`s pin their
  charts' images in their values; a chart upgrade moves the digests with its
  version, as the comment on `kube-prometheus-stack`'s release says how.

## Developer Environment

- **Model:** a single shared checkout on the Windows filesystem (NTFS) is used
  by both sides — no separate clones.
- **Client↔server local testing:** the dev container publishes `augustad`'s
  default listen port (UDP 27015) on the host, so a native Windows `augustac`
  reaches a container-hosted `augustad` at `127.0.0.1:27015`. Published rather
  than forwarded: VS Code's port forwarding is TCP-only.
- **Server, shared core and Linux client (Linux, dev container or host):**
  `.devcontainer/` gives this side as a container, for VS Code or Codespaces,
  without mutating a host, and `scripts/bootstrap.sh` installs the same on an
  Ubuntu 26.04 host (its Linux half, `scripts/bootstrap/linux.sh`; the image
  hashes only the two, so a Windows-only change doesn't rebuild it). The script
  is the one recipe for it: the image runs its `toolchain` step, the container's
  post-create its `checkout` step (submodules, vcpkg, hooks), and a host both.
  It installs CI's runner Ubuntu release's packages, whose LLVM major is CI's,
  with the toolchain `.github/actions/setup-linux-build` installs (kept in step
  with it by hand), LLVM's toolchain (ADR-0008: clang, lld, libc++, libunwind,
  LLVM's binary tools, lldb), CMake, Ninja, vcpkg, clang-tidy and ctcache (which
  `make tidy` runs it through, at CI's pinned commit), clang-format, GitHub CLI,
  kubectl, helm, the flux CLI at the cluster's Flux version, Doxygen, the hooks'
  formatters and linters (uv for yamllint, ruff, shfmt, shellcheck, actionlint,
  gersemi, Prettier and pymarkdown, standalone yamlfmt, StyLua, luacheck and
  taplo, at CI's pinned versions), and CI's Linux vcpkg binary cache
  configuration (a files provider in the checkout's `.vcpkg-bincache`). The
  image builds for the host's architecture (amd64 or arm64) rather than
  emulating CI's amd64. sccache's cache lives in a volume shared by every
  container of the repository; the build trees in a volume per container, so
  they never collide with a Windows build of the same checkout. One recipe, not
  a container beside a separate host bootstrap: two recipes for the same
  toolchain drift apart. With them come the Linux client's system packages
  (ADR-0008: X11, Wayland and xkbcommon development files, and Mesa's Vulkan
  drivers and Xvfb for the Renderer's smoke test), so the `linux` presets build
  and test `augustac` in the container too. The Windows client has no container
  equivalent (see below).
- **macOS (through the dev container):** nothing builds natively on macOS (the
  presets are Linux's and Windows'), and nothing is installed there but git, Git
  LFS and Docker or Podman, which `scripts/bootstrap.sh` installs through
  Homebrew (its macOS half, `scripts/bootstrap/macos.sh`, which also creates the
  container). `scripts/dev-container.sh [command]` runs a command (a shell by
  default) in the dev container's image from a terminal: one container per
  checkout, kept running, with the checkout mounted at its host path so a git
  worktree's `.git` resolves inside too, a `build/` volume of its own, and
  sccache's and vcpkg's caches in volumes every checkout shares.
  `scripts/dev-container.sh make configure PRESET=linux-debug` gives the hooks
  the compile commands clang-tidy needs. On macOS the `pre-commit` and
  `pre-push` hooks run their formatters and linters through it; git itself
  (identity, signing, credentials, LFS) stays on the host. A second bootstrap of
  the hooks' tools for macOS would drift from the container's, as two recipes
  for one toolchain do.
- **Client (Windows, native):** built and run natively — never cross-compiled
  from Linux: vcpkg's dependencies build with MSVC `cl`. The client, the server
  and their tests compile with `clang-cl` and links with `lld-link`, from the
  same pinned LLVM as the hooks' `clang-format` and `clang-tidy`, inside the
  Build Tools' developer environment (`scripts/vcenv.cmd`, which the Makefile
  runs every command through), whose MSVC STL, CRT and Windows SDK they build
  against; the dependencies stay on `cl` under vcpkg's `x64-windows` triplet
  (ADR-0008). The same `scripts/bootstrap.sh`, run in Git Bash as Administrator
  (its Windows half, `scripts/bootstrap/windows.sh`, winget-driven; one entry
  point and one `checkout` step and set of pinned formatter versions for both
  platforms, and no PowerShell: Git for Windows is the one prerequisite, which
  cloning needs anyway) installs Visual Studio Build Tools system-wide (default
  install location) — simpler than pinning a project-specific path, at the cost
  of not being able to side-by-side independent Build Tools versions per project
  — plus the Windows SDK, CMake, Ninja, GNU make, vcpkg, Git, uv (for yamllint
  and the formatters and linters uv runs, below, and ctcache, which `make tidy`
  runs `clang-tidy` through), standalone yamlfmt, StyLua, luacheck and taplo,
  and LLVM (`clang-cl` and `lld-link` for the build, `clang-format`/`clang-tidy`
  for the hooks below), pinned to the LLVM version CI's Ubuntu runner ships and
  CI's Windows jobs install, so a local build and the hooks agree with CI's
  gates, and kubectl, helm and the flux CLI, for the cluster. It also puts Git's
  `bin` directory on PATH, so `bash scripts/<script>.sh` runs from PowerShell
  and cmd, and the workspace's VS Code settings point the Kubernetes extension
  and its terminals at the day-to-day kubeconfig, `~/.kube/augusta.yaml`.
- **Asset cooker setup (opt-in):** `tools/pack/scripts/bootstrap.sh` (Windows in
  Git Bash, or Linux - the platforms vcpkg's `ktx` port, which the cooker's
  `_native` wraps, builds for) builds the pack environment under a caller-chosen
  assets root (ADR-0030): a uv-managed Python environment with `tools/pack`
  installed editable, its native module (built with MSVC `cl`, under its own
  presets), signing keys and sample authoring content.
- **USD Composer setup (authoring-only, opt-in):**
  `tools/composer/scripts/bootstrap.sh` (Windows in Git Bash, or Linux) builds
  NVIDIA Omniverse USD Composer via kit-app-template and fetches Adobe's
  USD-Fileformat-plugins under the same assets root. These heavier,
  GPU-dependent tools are deliberately kept out of `scripts/bootstrap.sh` and
  are never linked into shipped binaries (ARCHITECTURE.md §2); only content
  authors need them. `meshoptimizer` and libktx are `tools/pack/native`'s own
  C++ build dependencies (two small pybind11 modules, no OpenUSD - see ADR-0030)
  — vendored via `vcpkg.json` (ADR-0025) like the rest of the codebase, not
  fetched by this script.
- **Editor experience:** a committed `.vscode/extensions.json` lists recommended
  extensions (C++ tools, CMake Tools, clangd/clang-format, EditorConfig, Lua,
  YAML/Helm, GitHub Actions) — VS Code prompts to install these whenever the
  folder is opened, on either side (dev container or native Windows); the dev
  container installs the Linux-relevant subset itself.
- **Dependency hermeticity:** the `vcpkg.json` manifest (ADR-0025) is what
  actually makes dependency acquisition reproducible on both sides — not a
  container.

## Code Quality

- Google C++ Style Guide (ADR-0012), enforced via `clang-format` and
  `clang-tidy`; YAML uses the standalone `yamlfmt` v0.21.0 binary (two-space
  indentation) and `yamllint` 1.37.1. `clang-format` and `yamlfmt` auto-format
  staged files in the local `pre-commit` hook; `clang-tidy` checks changed C++
  and `yamllint` checks changed YAML in `pre-push`. CI's `format` job runs the
  formatters in check mode and strict YAML lint as the actual gate, since hooks
  can be skipped (`--no-verify`) or missing/mismatched locally. `clang-tidy`
  needs a full `compile_commands.json`, so it stays out of the commit hook and
  runs on changed C++ before push. `make format` applies the formatters,
  `make format-check` checks formatting and every linter but `clang-tidy`,
  `make lint` also runs `clang-tidy`, and `make tidy` alone runs `clang-tidy`,
  through ctcache as CI's Lint steps do (CI, clang-tidy results), keeping its
  results under `build/ctcache`; both bootstraps install it.
- A scenario's Lua scripts (ADR-0022, ADR-0039) are formatted by StyLua 2.5.2
  (`stylua.toml`: two-space indentation, 120 columns, as the C++) and linted by
  luacheck 1.2.0 (`.luacheckrc`), whose standard library is the engine's Lua
  sandbox written out rather than a stock Lua's: what the sandbox takes out
  (`print`, `pcall`, `io`, `os`, `math.random`...) is a warning, and what the
  engine gives a script is known only where it is given - `server.tick_rate_hz`
  to a Parameters script, each Game policy hook to the rules. StyLua formats
  staged scripts in `pre-commit`, luacheck checks changed ones in `pre-push`,
  and CI's `format` job runs both in check mode.
- TOML files are formatted and linted by taplo 0.10.0 (`.taplo.toml`: the same
  width and indent, arrays kept one entry a line where written so), which is
  also the Even Better TOML extension's engine: it formats staged files in
  `pre-commit`, lints changed ones in `pre-push`, and CI's `format` job runs
  both in check mode.
- Python, shell, the workflows, CMake and Markdown each have a formatter, a
  linter or both, every one run by uv at a pinned version (`uv tool run`), so
  nothing is installed for them but uv. Python follows the Google Python Style
  Guide (ADR-0012): ruff 0.16.10 formats and lints (`ruff.toml`: 80 columns; the
  guide's checks - pylint's, naming, Google-convention docstrings, one import
  per line, no relative imports - and bugbear, pyupgrade and simplify). Shell
  scripts and the git hooks follow the Google Shell Style Guide (ADR-0012), in
  Bash: shfmt 4.2.0 formats (by `.editorconfig`: two-space indent, indented
  `case` patterns, a continued `|` or `&&` starting the next line) and
  shellcheck 0.11.0 lints (`.shellcheckrc`: the guide's optional checks,
  `[[ ]]`, braced and quoted expansions). The workflows: actionlint 1.7.12
  (`.github/actionlint.yaml`), with shellcheck on their `run:` scripts. CMake:
  gersemi 0.29.2 formats (`.gersemirc`: 120 columns, two-space indent, the
  project's own functions read from `cmake/`). Markdown follows the Google
  Markdown style guide (ADR-0012): Prettier 3.9.9 formats (`.prettierrc.yaml`),
  with Node run from its PyPI wheel: paragraphs wrapped at 80 columns, a nested
  list or a block in a list item indented 4 spaces (as the guide asks and MkDocs
  needs), and code blocks left as written. pymarkdown 0.9.40 lints with
  markdownlint's rules (`.pymarkdown.json`) set to the guide (80 columns but for
  headings, tables, code blocks and a long URL; ATX headings; fenced code
  blocks; no trailing whitespace), less what the guide leaves to the writer
  (ordered-list numbering, emphasis as a heading) and what Prettier decides
  (blank lines around lists, table alignment). The repository has no PowerShell
  scripts: its scripts are Bash, and the one Windows-native helper,
  `scripts/vcenv.cmd`, is a batch file. The formatters run on staged files in
  `pre-commit`, the linters on changed files in `pre-push`, and CI's `format`
  job runs all of them in check mode.
- JSON has no tool of its own: `CMakePresets.json` and `vcpkg.json` are
  validated by CMake and vcpkg on every configure, and the `.vscode` files are
  the editor's, formatted by it on save.
- Strict warnings-as-errors in CI (see CI/CD above).
- ASan/UBSan and fuzzing in CI; TSan nightly given multithreading (ADR-0005,
  ADR-0013).
- **Commit messages:** Conventional Commits format, enforced locally via a
  custom `commit-msg` git hook (a small regex-matching script) — no
  Node.js/`commitlint` dependency, consistent with keeping the toolchain to what
  the project already uses (C++, Lua, Python for asset tooling).
- Testing: GoogleTest, RapidCheck (property-based), libFuzzer, pytest (asset
  cooker), and Google Benchmark (micro-benchmarks); which kind runs at which
  stage is ADR-0013.
- No formal code review process — solo project; CI's build, test, lint, and
  sanitizer gates are the primary quality gate.

## Observability

- **Logging:** Boost.Log (ADR-0036) — mature, no reason to hand-roll one given
  the project's learning focus is elsewhere (ballistics, networking, ECS).
- **Profiling:** NVTX ranges in the code, read with NVIDIA Nsight Systems (CPU
  threads, timeline) and Nsight Graphics (GPU frames, D3D12 capture) — the
  primary tools for inspecting client and server performance during development.
- **Metrics:** `augustad` serves Prometheus metrics for its tick, Lobby and
  Match, Sessions and each client's Connection health, which a
  kube-prometheus-stack in the k3s cluster scrapes and Grafana draws, for both
  `develop` and `staging` (ADR-0049, which holds the catalogue). Everything is
  measured on the server: clients report nothing.
- **Log aggregation:** Loki keeps every pod's log for 15 days, shipped by Alloy,
  and Grafana queries it beside the metrics (ADR-0053).
- **Liveness:** `/livez` fails when the tick loop has not finished a tick for 5
  seconds, and is the Deployment's liveness probe, so a hung server restarts on
  its own (ADR-0049).
- **Alerts:** the servers' rules ship with the chart, the cluster's (Flux) with
  the monitoring stack's release. Every `critical` or `error` alert, a crash
  loop and a stalled rollout reach the operator's Telegram chat; the rest show
  only in Grafana and Alertmanager (ADR-0049).
- **Grafana** opens on node port 30300 with no login: anyone on the LAN is an
  anonymous Viewer, and there is no login form, basic authentication or admin
  user (ADR-0057). Nothing is changed in Grafana itself. A dashboard is changed
  in its JSON in Git, `charts/augustad/dashboards/`: export it from Grafana (or
  edit it there in a local Grafana) and commit the file; once the chart deploys,
  Grafana picks it up within 30 seconds. Data sources are the
  `kube-prometheus-stack` release's values
  (`clusters/onprem/infrastructure/monitoring.yaml`), loaded as Grafana starts.

## Performance

- **NFR-01** (server tick rate ≥ 60 Hz, see REQUIREMENTS.md) remains the only
  formal, tested performance target.
- **No formal client frame-rate target.** Deliberately not turned into an NFR —
  frame rate is judged subjectively while playing/testing, not automated or
  gated in CI.
- **Memory strategy:** rely on Flecs' and PhysX's built-in allocators; no custom
  arena/pool allocators until profiling shows a concrete need.
- Google Benchmark is used for targeted micro-benchmarks of hot-path code (the
  server tick and its capture, replication, a PresentationWorld frame,
  ballistics, serialization, pack loading) — not a blanket requirement for every
  function. The nightly tracks them over time (ADR-0013).

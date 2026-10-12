# Flux Runs Only Signed Revisions; `staging` Follows Release Tags

Flux verifies the signature of every revision it reconciles, so a push that
reaches GitHub without a trusted signature never runs on the Node. `staging`
follows the signed `v*` release tags rather than the `main` branch (amending
ADR-0026, which had it track `main`). `develop` runs the chart CI signed for its
branch's latest commit, with values from the branch's signed HEAD. An
Environment rolls out a revision only once CI has published that revision's
image, which the chart runs (`sha-<12>` of its commit, ADR-0026).

**`staging`: release tags, signed by the maintainer.** Its `GitRepository`
selects the latest `v*` tag (`ref.semver`) and verifies it in `Tag` mode against
the maintainer's SSH signing key alone. The cut signs those tags
(`docs/runbooks/cut-release.md`), and only once `main`'s CI has published the
tagged commit's multi-architecture image, so `staging` runs only what the
maintainer signed as a release, never what a merge to `main` merely made
reachable, and never before its image exists.

**`develop`: the chart CI signed, the values of its signed HEAD.** On `develop`
alone, once a commit's image is published and signed, CI packages the chart as
`<chart version>+<12>` and pushes it to GHCR, under the immutable tag
`<chart version>_<12>` and the moving tag `develop`. CI signs it keyless with
cosign, as the same workflow signs the image. `develop`'s `OCIRepository`
follows the tag `develop` and accepts only the identity
`ci.yml@refs/heads/develop` of GitHub Actions' OIDC issuer: the identity the
cluster already trusts the image by (ADR-0057), narrowed to `develop`. A commit
whose CI fails before the chart is published leaves `develop` on its previous
chart.

The `HelmRelease`'s own spec and values (`packVersion`, node ports, captures)
stay in Git, from the `GitRepository` that verifies `develop`'s HEAD against the
maintainer's key and GitHub's `web-flow` key. A pull request merged on GitHub is
a merge commit signed by `web-flow`, and branch protection (a pull request and
signed commits, required on `main` and `develop`) is what limits what `web-flow`
signs. A pack publish therefore rolls out without waiting for CI, and so does
any infrastructure or policy change, which stays on Git as well. Since chart and
values move apart, renaming a value `develop` sets takes two steps: the chart
first, then the values once its chart is live.

The trusted public keys are a Secret in Git, unencrypted: they are public, and
the cluster keeps no secret (ADR-0057). Keyless signatures need no key at all:
the identity is the workflow that signed.

## Considered Options

- **Branch protection only, no verification in Flux**: rejected. A token or
  account able to push past it would run code on the Node with nothing in the
  cluster to notice.
- **The maintainer's key alone for both environments, merging locally**
  (`git merge --no-ff -S`, with a bypass of the pull request rule): rejected. It
  trades the pull request flow, and the CI checks it gates, for a guarantee
  `develop` does not need.
- **OpenPGP signing to suit Flux**: unnecessary. Flux 2.9 verifies SSH
  signatures (`.sshpub` keys), the format the repository's commits and tags
  already use.
- **`develop` on a Git ref CI moves once the image is published**: rejected. A
  branch CI writes breaks Git Flow's branch naming, and any commit `web-flow`
  ever signed, however old, could be pointed at.
- **Helm's patience: a longer progress deadline and retries, so an upgrade waits
  for its image**: rejected. A commit whose CI fails still ships its chart, and
  the pod sits in `ImagePullBackOff` until the image exists or never does.

## Consequences

- A merge to `main` changes nothing that runs: `staging` moves at the next
  signed tag. A hotfix reaches it when its tag is cut.
- Rolling `staging` back is pinning its `GitRepository` to an older tag, not
  reverting `main`; rolling `develop` back is pinning its `OCIRepository` to an
  older immutable `<chart version>_<12>` tag (`docs/runbooks/flux-rollback.md`).
- A chart pushed or signed by any other identity, `ci.yml` on `main` or a
  feature branch included, fails verification, and Flux reconciles nothing from
  it.
- Flux's source-controller verifies keyless signatures online, against
  Sigstore's trusted root and transparency log, over the HTTPS egress it already
  has.
- helm-controller keeps the published chart's version as CI set it, rather than
  replacing its build metadata with the artifact's digest
  (`DisableChartDigestTracking`): the chart takes its image tag from it.
- Rotating the maintainer's signing key, or GitHub rotating `web-flow`'s, stops
  reconciliation until the trusted keys in Git are updated, from a commit the
  old keys still verify.

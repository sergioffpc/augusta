# Flux Runs Only Signed Revisions; `staging` Follows Release Tags

Flux verifies the signature of every revision it reconciles, so a push that
reaches GitHub without a trusted signature never runs on the Node. `staging`
follows the signed `v*` release tags rather than the `main` branch (amending
ADR-0026, which had it track `main`), and `develop` keeps following its branch.

**`staging`: release tags, signed by the maintainer.** Its `GitRepository`
selects the latest `v*` tag (`ref.semver`) and verifies it in `Tag` mode against
the maintainer's SSH signing key alone. The cut already signs those tags
(`docs/runbooks/cut-release.md`), so the release takes no new step, and
`staging` runs only what the maintainer signed as a release, never what a merge
to `main` merely made reachable.

**`develop`: its branch HEAD, signed by the maintainer or by GitHub.** A pull
request merged on GitHub is a merge commit signed by GitHub's `web-flow` key,
not the maintainer's, so `develop`'s `GitRepository` verifies its HEAD against
both keys. Branch protection (a pull request and signed commits, required on
`main` and `develop`) is what limits what `web-flow` signs.

The trusted public keys are a Secret in Git, unencrypted: they are public, and
the cluster keeps no secret (ADR-0057).

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

## Consequences

- A merge to `main` changes nothing that runs: `staging` moves at the next
  signed tag. A hotfix reaches it when its tag is cut.
- Rolling `staging` back is pinning its `GitRepository` to an older tag, not
  reverting `main` (`docs/runbooks/flux-rollback.md`).
- Rotating the maintainer's signing key, or GitHub rotating `web-flow`'s, stops
  reconciliation until the trusted keys in Git are updated, from a commit the
  old keys still verify.

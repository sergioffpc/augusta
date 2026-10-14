# Build, dependencies, configuration and deployment standard

**Objective:** the same commit builds the same artefacts on every supported
platform, dependencies are pinned and accounted for, configuration is validated,
and what is deployed is what was tested.

## Questions

- Are all dependency versions pinned (manifest, lock file, submodule commit,
  tool versions)? Is anything fetched at build time unpinned?
- Do the build presets or targets differ between platforms in ways that change
  behaviour (flags, defines, feature switches)? Is each difference intended and
  documented?
- Do warnings, sanitizers and static analysis run on the code that ships, or on
  a different configuration?
- Is generated code or content (shaders, packs, protocol tables) regenerated
  from source, and is a stale copy detectable?
- Configuration: is every value validated at load, with a clear error? Are
  defaults safe? Can two sources of configuration disagree?
- Deployment: is the deployed artefact traceable to a commit and a passing CI
  run? Are images and charts scanned and signed as documented? Are secrets
  injected rather than baked in?
- Licensing: do new dependencies meet the project's licence constraints?

## Failure patterns

- A dependency resolved to "latest" in one path but pinned in another.
- A define set only in the release preset that changes behaviour tests never
  see.
- A config key misspelt and silently ignored.
- An image tag reused for different content.
- CI testing a configuration that is not the one deployed.

## Evidence required

The two configurations or versions that diverge, cited, and the behaviour or
artefact difference it causes.

## Verification

Diffing presets and workflow steps; a clean configure from scratch in a
throwaway worktree; loading an invalid config in a test; inspecting artefact
metadata (SBOM, signatures) as CI publishes it.

# Client Release: Its Whole Runtime Folder, Zipped

A release ships the client as `augustac-windows-x64.zip`, its whole runtime
folder, never as a lone `augustac.exe`. The folder holds:

- `augustac.exe`, with the renderer's shaders and the debug HUD's font in it
  (ADR-0059);
- the DLLs CMake resolves for it: vcpkg's, and Aftermath's when the build has
  it;
- the MSVC runtime.

Unzipped anywhere, the client runs with no install step and no Visual C++
redistributable.

**Why not a static `.exe`.** Steam Audio and Aftermath ship only as DLLs, so a
static build would still need the folder beside it. vcpkg's other ports could be
static, but that saves a few DLLs of a folder that must exist anyway.

**How it is made.** The client's install rules, component `client`, stage the
folder (`cmake --install --component client`). They are `EXCLUDE_FROM_ALL`, so
`augustad`'s own install is unchanged. `scripts/stage-client.sh` stages and zips
the folder, and runs `augustac --version` from its unzipped copy with only
Windows' own folders on `PATH`. CI's client job runs it on every pull request.
The release workflow publishes the zip only once that copy prints the tag's
version. The zip has the release's provenance and SBOM attestations. The PDB
stays a separate asset (ADR-0055).

## Considered Options

- **A lone `.exe`**, as releases up to v3.0.0 shipped: rejected. It loads
  nothing on a machine without a build tree.
- **A static `.exe`**: rejected, as above. The DLL-only dependencies need the
  folder anyway.
- **An installer (MSI or MSIX)**: rejected for now. It adds signing,
  install/uninstall and upgrade paths, none of which a playtest client needs.
  The staged folder is what an installer would package later.

## Consequences

- The client asset is about 18 MB zipped (45 MB unzipped).
- `augustac --version` exits before the Renderer starts, so the check catches a
  missing DLL that `augustac` imports, not one loaded only once it runs. A
  hosted runner has the VC++ redistributable in `System32`, so the check does
  not show that the bundled MSVC runtime is complete either.
- The SBOM is made from the repository's vcpkg tree alone, with NVTX's commit
  and the MSVC runtime the zip ships (`scripts/vcpkg-sbom.py`). Like every SBOM
  made from that tree, it lists all the manifest's ports for the triplet, test
  and build-tool ports included.

# Client Release: Its Whole Runtime Folder, Zipped

A release ships the client as `augustac-windows-x64.zip`, its whole runtime
folder, never as a lone `augustac.exe`. The folder holds:

- `augustac.exe`;
- the DLLs CMake resolves for it;
- Falcor's runtime: its DLLs, the ones it loads itself, and its shaders, data,
  plugins and D3D12 Agility SDK;
- the renderer's shaders;
- the MSVC runtime.

It does not hold Falcor's PDBs. Unzipped anywhere, the client runs with no
install step and no Visual C++ redistributable.

**Why not a static `.exe`.** Falcor (ADR-0009) is a DLL. At runtime it compiles
shaders with slang and dxcompiler, and reads its shaders, data and plugins from
the executable's folder. PhysX GPU, DLSS, Aftermath and Steam Audio ship only as
DLLs. So a static build would still need the folder beside it. vcpkg's ports
could be static, but that saves a few DLLs of a folder that must exist anyway.

**How it is made.** The client's install rules, component `client`, stage the
folder (`cmake --install --component client`). They are `EXCLUDE_FROM_ALL`, so
`augustad`'s own install is unchanged. `scripts/stage-client.sh` stages and zips
the folder. The release workflow publishes the zip only once its unzipped copy
runs `augustac --version` and prints the tag's version, with only Windows' own
folders on `PATH`. CI's client job runs the same check, without the zip, on
every pull request. The zip has the release's provenance and SBOM attestations.
The PDB stays a separate asset (ADR-0055).

## Considered Options

- **A lone `.exe`**, as releases up to v3.0.0 shipped: rejected. It loads
  nothing on a machine without a build tree.
- **A static `.exe`**: rejected, as above. Falcor's folder is needed anyway.
- **An installer (MSI or MSIX)**: rejected for now. It adds signing,
  install/uninstall and upgrade paths, none of which a playtest client needs.
  The staged folder is what an installer would package later.
- **Falcor's PDBs in the zip**: rejected. They are 749 MB of a 1.3 GB folder,
  and no player reads them. Only `augustac.pdb` is published, apart from the
  zip.

## Consequences

- The client asset grows from about 1 MB to about 195 MB zipped (490 MB
  unzipped).
- `augustac --version` exits before Falcor starts, so the check catches a
  missing DLL that `augustac` imports. It does not catch anything Falcor loads
  once it runs. A hosted runner has the VC++ redistributable in `System32`, so
  the check does not show that the bundled MSVC runtime is complete either.
- The SBOM lists the vcpkg ports and the Falcor and NVTX commits. It does not
  list what Falcor fetches with packman, nor the MSVC runtime, though the zip
  ships both.

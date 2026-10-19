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
Windows' own folders on `PATH`. CI's `client-x64-windows` job runs it on every
pull request. The release workflow publishes the zip only once that copy prints
the tag's version. The zip has the release's provenance and SBOM attestations.
The PDB stays a separate asset (ADR-0055).

## The Linux Tarball

A release also ships the Linux client as `augustac-linux-x64.tar.gz`: one
`augustac/` folder of the same shape as the zip, staged by the same `client`
install component. Extracted anywhere, `augustac/augustac` runs as it is, with
no launcher script.

- **What it holds.** `augustac`, and the shared libraries it links that are not
  the system's, found beside it through `RPATH=$ORIGIN`. vcpkg links its ports
  statically on Linux (Steam Audio included), and the toolchain links the C++
  runtime and its unwinder statically (ADR-0008), so today the folder bundles no
  library at all; the install rules bundle one that a dependency ships only as a
  shared library.
- **What it links from the system.** Only what an allowlist in
  `scripts/stage-client.sh` names: glibc, the Vulkan loader (ADR-0059), and the
  X11, Wayland, xkbcommon and audio libraries that GLFW and miniaudio load. A
  library outside it, neither bundled nor allowed, fails the staging check, so a
  new system dependency is a conscious decision.
- **Where it runs.** It is built on CI's `ubuntu-26.04` runner, so it needs the
  glibc of Ubuntu 26.04 or newer, as the README and each release's notes state.
  An older baseline is a build change (an older build container), not a code
  change.
- **How it is checked.** On Linux, `scripts/stage-client.sh` tars the staged
  folder, extracts it into an empty one, and runs `augustac --version` there
  with no `LD_LIBRARY_PATH` or `LD_PRELOAD`, checking the printed version when
  given one. It checks each file's `NEEDED` entries against the folder and the
  allowlist, and that `augustac.debug` has the build ID of the `augustac` in the
  folder (ADR-0055). The nightly `client-x64-linux` job runs it, and the
  release's `client-x64-linux` job before it publishes the tarball, its `.debug`
  and its SBOM, with the same provenance and SBOM attestations as the zip. Both
  jobs also run the extracted `augustac --version` in a clean `ubuntu:26.04`
  container, so none of the build runner's libraries can stand in for one the
  folder lacks. ctest checks the check itself, against a stand-in build
  (`checks/stage_client_linux.cmake`), on every pull request's Linux build.

## Considered Options

- **A lone `.exe`**, as releases up to v3.0.0 shipped: rejected. It loads
  nothing on a machine without a build tree.
- **A static `.exe`**: rejected, as above. The DLL-only dependencies need the
  folder anyway.
- **A Linux launcher script setting `LD_LIBRARY_PATH`**: rejected. `$ORIGIN`
  finds the bundled libraries without one, and the variable would leak into
  every process the client starts.
- **AppImage, Flatpak or the Steam Linux Runtime**: rejected for now. Each adds
  a format and its tooling; the tarball is what any of them would package.
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

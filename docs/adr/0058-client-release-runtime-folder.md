# Client Release: Its Whole Runtime Folder, Zipped

A release ships the client as `augustac-windows-x64.zip`, its whole runtime
folder, never as a lone `augustac.exe`. The folder holds:

- `augustac.exe`, with the renderer's shaders and the debug HUD's font in it
  (ADR-0059);
- the DLLs CMake resolves for it: vcpkg's, and Aftermath's when the build has
  it.

Unzipped anywhere, the client runs with no install step of its own. The MSVC
runtime is the one thing it takes from the system beyond Windows itself (see The
MSVC Runtime below).

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

## The MSVC Runtime

The zip bundles no `vcruntime140*`, `msvcp140*` or other MSVC runtime DLL. The
build's sysroot has no redistributable DLLs to bundle (ADR-0079), so the player
installs the Microsoft Visual C++ Redistributable (x64).

- **The minimum version.** `MSVC_REDIST_VERSION` in
  `scripts/bootstrap/versions.env`: the file version of the `msvcp140.dll` that
  the pinned toolset's redistributable ships, bumped with the sysroot. An older
  runtime can crash the client with no useful error: the 17.10 STL's
  `std::mutex` against an older `msvcp140.dll` is the known case.
- **Where it is stated.** The README and each release's notes name it. The
  release workflow writes the notes' line from the pin, and CI fails when the
  README names another version.
- **The startup check.** `augustac` reads the version of the `msvcp140.dll` it
  loaded before it parses its command line, and exits naming the version to
  install when it is older or cannot be read. So `augustac --version`, the
  staging check's run, fails on such a runtime too.
- **How it stays out of the folder.** The install rules exclude the runtime's
  DLLs wherever CMake would resolve them, and `scripts/stage-client.sh` fails a
  folder that holds one.

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
- **Bundling the MSVC runtime**: rejected. The build's sysroot has no
  redistributable DLLs to bundle (ADR-0079), and ADR-0079 says why neither way
  left, extracting them from a pinned `vc_redist.x64.exe` or a static CRT, is
  taken.
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
  missing DLL that `augustac` imports, not one loaded only once it runs.
- A player installs the VC++ redistributable before the first run, and again
  when a release raises its minimum version. A hosted runner has a recent one in
  `System32`, which the staging check's run loads.
- The SBOM is made from the repository's vcpkg tree alone, with NVTX's commit
  (`scripts/vcpkg-sbom.py`). It lists no MSVC runtime, which the zip does not
  ship. Like every SBOM made from that tree, it lists all the manifest's ports
  for the triplet, test and build-tool ports included.

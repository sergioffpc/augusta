# Windows Builds Against a Pinned xwin Sysroot, With No Visual Studio

ADR-0008 made `clang-cl` the compiler of our own code on Windows but kept the
Visual Studio Build Tools: `clang-cl` took the MSVC STL, the CRT and the Windows
SDK from them, and vcpkg's ports still built with MSVC `cl`. Windows was then
the one platform with two compilers and a toolchain that `versions.env` does not
pin. So every Windows build, the ports included, now compiles with the pinned
LLVM's `clang-cl` against a sysroot that xwin downloads at a pinned version, and
no Visual Studio is installed or used. Windows still builds on Windows: each
platform builds on its own OS, Linux on Linux and macOS on macOS, and nothing is
cross-compiled.

**One toolchain file.** `cmake/LlvmWindowsToolchain.cmake` names `clang-cl`,
`lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt` and `llvm-ml64`, with `/winsysroot`
at the sysroot under `.toolchain/` (`llvm-rc`, which takes none, gets its
include directories). The sysroot's version passes into each port's ABI, so a
bump rebuilds them. A ctest check reads the build's header dependencies and
fails on any header from outside the sysroot, vcpkg's tree, the repository and
LLVM, as one from a Visual Studio installed beside them. The `windows*` presets
and `triplets/x64-windows.cmake` chainload it, as `LlvmToolchain.cmake` and
`LlvmMacosToolchain.cmake` are chainloaded on Linux and macOS. The presets keep
their `hostSystemName == Windows` condition.

**The sysroot is a pinned tool.** xwin's version and the MSVC and SDK manifest
versions it fetches are in `scripts/bootstrap/versions.env` (ADR-0077).
`make toolchain` puts the sysroot, the STL, the CRT, the Windows SDK and ATL
(which vcpkg's `llvm` port needs), in the store, with the debug CRT's libraries,
which every Debug build links, and its DLLs, which a Debug build's tests load
where no Visual Studio is installed. The bootstrap installs no Build Tools, and
`scripts/vcenv.cmd` and `.github/actions/setup-windows-msvc` go. The repository
is public and the sysroot is Microsoft's, under the license xwin accepts on each
download: it is downloaded on each machine and kept in CI's cache, and never
committed, put in an image (the `Dockerfile` included) or put in a release
asset.

**Every port compiles with `clang-cl`, Mesa and LLVM included.** Some ports
needed msbuild or nmake, which ship only with Visual Studio. Four of them
change:

- **libsodium** is built by an overlay port with CMake, statically. vcpkg's port
  uses msbuild under `cl` and refuses a shared build under any other compiler.
  Its one consumer is the assets module, so a static copy is never duplicated.
- **PhysX**'s overlay drops `WINDOWS_USE_MSBUILD` and builds with Ninja through
  the chainload. The triplet takes the clang 21 `-Wno-*` that `x64-linux.cmake`
  already passes it.
- **GameNetworkingSockets** is overlaid with `USE_CRYPTO=BCrypt` on Windows.
  This drops OpenSSL from the Windows graph, whose port builds with nmake even
  when it uses jom. Linux and macOS keep OpenSSL.
- **pybind11** is overlaid without its `python3` dependency, whose port builds
  CPython with msbuild. The tools' Python modules compile against the headers
  and the `pythonXY.lib` of the Python that `augusta_find_tools_python` finds.

MASM is LLVM's `llvm-ml64`, which assembles what Visual Studio's `ml64` does but
`PROC ... EXPORT`. Boost.Context, which boost-asio's default `spawn` feature
pulls in and none of our code calls, switches contexts in such MASM, so on
Windows the triplet builds it on Windows' fibers (`winfib`), with no assembly.

**The player installs the MSVC runtime.** The sysroot has no redistributable
DLLs, and the only redistributable Microsoft publishes moves under a permalink.
So the client zip no longer bundles `vcruntime140*` and `msvcp140*` (amending
ADR-0058):

- The README and each release's notes name the minimum VC++ redistributable,
  that of the pinned sysroot's STL: the file version of the `msvcp140.dll` its
  toolset's redistributable ships, which `versions.env` pins
  (`MSVC_REDIST_VERSION`) beside the toolset, as it cannot be derived from the
  toolset's version, and which is bumped with it.
- `augustac` checks at startup that the `msvcp140.dll` it loaded is at least
  that version, and exits naming the version to install. Without the check it
  would crash later; the `std::mutex` change of the 17.10 STL is the known case.
- The SBOM no longer lists the MSVC runtime.

## Considered Options

- **Only the compiler (`clang-cl` for the ports), keeping the Build Tools for
  the STL, CRT and SDK**: one compiler, but Windows keeps a toolchain that no
  pin covers and an Administrator install. It is the first step, not the end.
- **llvm-mingw (libc++, UCRT, the `x86_64-w64-mingw32` target)**: no Microsoft
  library at all, but its C++ ABI is not MSVC's. NVIDIA's binaries (PhysX's GPU
  DLLs, Aftermath), the Nsight tools and the PDBs expect MSVC's, and vcpkg's
  mingw triplets are community-maintained.
- **Cross-compiling Windows (and macOS) from Linux**: one build host, but LLVM
  and Mesa are the ports least likely to cross-compile, the result cannot be
  tested on the build host, and Apple's SDK license allows only Apple hardware.
  Each OS builds on itself.
- **Bundling a runtime extracted from a pinned `vc_redist.x64.exe`**: keeps
  ADR-0058's no-install promise, at the cost of a copy of the installer kept
  pinned by hash and a WiX bundle → MSI → CAB extraction for five DLLs.
- **A static CRT (`/MT`)**: no redistributable, but each of the triplet's DLLs
  would carry its own CRT.

## Consequences

- A build that adopts this rebuilds the whole Windows NuGet cache, because
  vcpkg's ABI hash covers the compiler and the toolchain file.
- Bumping the sysroot can raise the redistributable players need. The release
  notes, the README and the startup check follow `versions.env`.
- Visual Studio is optional: anyone may install it as an IDE or a debugger, but
  the build never uses its STL, CRT or SDK. `docs/agents/tooling.md` names
  WinDbg and the pinned LLVM's `lldb-dap` for debugging.
- The work lands in two steps, each with `client-x64-windows` green: first every
  port on `clang-cl` with the Build Tools still present, then the sysroot
  without them. ADR-0008, ADR-0058 and `docs/ENGINEERING.md` are amended as each
  step lands, since they describe the build as it is.

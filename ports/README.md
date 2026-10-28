# ports

vcpkg overlays of the ports this repository needs changed, which `vcpkg.json`'s
`overlay-ports` puts in place of the vcpkg submodule's (`third_party/vcpkg`).
Each starts as a copy of the submodule's port at the commit it pins: a change to
it is the diff from that copy, and a bump of the submodule re-copies the port
and re-applies the diff.

- `physx`: OmniPVD's runtime (`PX_BUILDPVDRUNTIME`), for
  `debug.physics_recording`; and macOS arm64, for the client's development build
  there. PhysX 5 has no macOS platform files: the port builds it with its Linux
  ones, without the PhysXGpu library NVIDIA ships for Linux and Windows alone,
  and `macos-arm64.patch` fixes the sources where they take `PX_OSX` for x86
  (`portfile.cmake` lists what it changes).
- `nvrhi`: macOS arm64 among its `supports`, for its Vulkan backend on Mesa's
  KosmicKrisp (docs/ENGINEERING.md).
- `pybind11`: without its `python3` dependency, whose port builds CPython with
  msbuild on Windows (ADR-0079). The port is headers and CMake files alone, and
  the tools' modules (tools/agent, tools/pack) are built against the Python
  `cmake/AugustaToolsPython.cmake` finds, not one vcpkg installs.

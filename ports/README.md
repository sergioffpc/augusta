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
  (`portfile.cmake` lists what it changes). On Windows it builds with Ninja
  through the chainloaded toolchain, not msbuild (ADR-0079).
- `nvrhi`: macOS arm64 among its `supports`, for its Vulkan backend on Mesa's
  KosmicKrisp (docs/ENGINEERING.md).
- `libsodium`: Windows built by CMake and Ninja, statically, from the
  `CMakeLists.txt` beside its portfile, in place of upstream's msbuild solution:
  it builds under `cl` and `clang-cl` alike (ADR-0079). Linux and macOS keep
  upstream's make path.

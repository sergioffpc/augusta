# Build Tooling

The build uses CMake + Ninja + sccache, for cross-platform, fast, cached builds
across the client, on Windows and Linux, and the Linux server.

The default build (`all`) is the binaries alone. The tests, benchmarks and
fuzzers are excluded from it and build through one `augusta_tests` target, which
`make test` builds before `ctest` and CI builds wherever it runs them: building
a binary never waits on the test suite.

Linux (the server, the client and the shared core, including the sanitizer and
fuzzing builds) builds with LLVM's toolchain alone, Ubuntu's packages of it:
clang compiles against libc++ and libc++abi, lld links, compiler-rt and
libunwind are the runtime, LLVM's binary tools (`llvm-ar`, `llvm-objcopy`, ...)
stand in for binutils, and lldb debugs and reads cores. libFuzzer, `llvm-cov`,
the sanitizers, `clang-tidy` and `clang-format` then come from that one
toolchain too. `cmake/LlvmToolchain.cmake` states it once, and vcpkg's toolchain
chainloads it twice: from the linux presets for the project, and from the
overlay triplets (`triplets/x64-linux.cmake`, `triplets/arm64-linux.cmake`) for
every port, so the dependencies are built with the compiler and standard library
they are linked with rather than the system's g++ and libstdc++. The sanitizer,
fuzzing and coverage presets give their flags as `CMAKE_<LANG>_FLAGS_INIT`,
which the toolchain's add to; a `CMAKE_CXX_FLAGS` would replace them. Every
binary of an ASan build (`linux-san`, `linux-fuzz`) runs with ASan's
container-overflow check off, compiled into it
(`cmake/asan_default_options.cpp`) so that it holds however the binary is run:
libc++ marks its containers' unused capacity for it, which holds only when every
caller is instrumented, and the dependencies are not, so GameNetworkingSockets
or GoogleTest using a vector reads as an overflow. libc++, libc++abi and
libunwind link statically: `augustad` needs nothing from the system but glibc
(ADR-0054), which stays, as nothing in LLVM replaces it, and `augustac` nothing
more than glibc and the libraries it loads at runtime (the Vulkan loader, X11 or
Wayland, and the sound server). The one GNU library a build still links is the
fuzzing build's: Ubuntu builds libFuzzer against libstdc++, so the `linux-fuzz`
preset links `libstdc++.so.6` beside libc++, whose own names, in `std::__1`,
never collide with it. Windows compiles with MSVC `cl`, which vcpkg's
`x64-windows` triplet and NVIDIA's SDKs are built and tested with; clang-cl was
rejected for that reason (see ENGINEERING.md, Developer Environment). The shared
core therefore builds with two compilers, which the NFR-03 golden trajectories
compare (ADR-0013).

The client (`augustac`, its Renderer and their tests) builds with the
`AUGUSTA_CLIENT` option, on by default on Windows and Linux, so
`cmake --preset linux` builds it as `cmake --preset windows` does. Its ports are
the vcpkg manifest's `client` feature, which the option installs: on Linux,
vcpkg's GLFW builds against the system's X11, Wayland and xkbcommon development
files, which `scripts/bootstrap/linux.sh` installs (and through it the dev
container), and loads the libraries themselves at runtime, choosing X11 or
Wayland by the session it finds. A build of the server alone turns the option
off: the server image, and every Linux CI job but the nightly's `client-linux`,
so pull requests build the client on Windows only (ADR-0013) and none of them
needs the client's ports or system packages.

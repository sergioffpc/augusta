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
or GoogleTest using a vector reads as an overflow. Every binary of a UBSan build
(`linux-san`) halts on UBSan's first report, with a stack trace, compiled in the
same way (`cmake/ubsan_default_options.cpp`, ADR-0013). libc++, libc++abi and
libunwind link statically: `augustad` needs nothing from the system but glibc
(ADR-0054), which stays, as nothing in LLVM replaces it, and `augustac` nothing
more than glibc and the libraries it loads at runtime (the Vulkan loader, X11 or
Wayland, and the sound server). The one GNU library a build still links is the
fuzzing build's: Ubuntu builds libFuzzer against libstdc++, so the `linux-fuzz`
preset links `libstdc++.so.6` beside libc++, whose own names, in `std::__1`,
never collide with it.

Windows compiles our code with `clang-cl` and links it with `lld-link`, from
LLVM 21.1.8, the version that already gives `clang-format` and `clang-tidy` on
both platforms: one LLVM version to bump. The `windows` and `windows-debug`
presets name them (`CMAKE_CXX_COMPILER` `clang-cl`, `CMAKE_LINKER_TYPE` `LLD`);
the Windows bootstrap installs that LLVM, and CI's and the release's Windows
client jobs install it through `.github/actions/setup-windows-llvm`. The Visual
Studio Build Tools and their developer environment (`scripts/vcenv.cmd`, CI's
`msvc-dev-cmd`) stay: `clang-cl` takes the MSVC STL, the CRT and the Windows SDK
from them, and sets CMake's `MSVC`, so the MSVC build settings apply to it
unchanged. Its `/W4 /WX` is clang's `-Wall -Wextra -Werror`, Linux's warning
set; `/Wall` would be `-Weverything`. The dependencies stay on MSVC `cl`, under
vcpkg's stock `x64-windows` triplet (`mesa[lavapipe]` included), with no custom
triplet: `clang-cl` is ABI-compatible with what `cl` builds, the same STL and
CRT, and those ports are built and tested upstream with `cl`. Our code is then
compiled by one compiler family everywhere: code that clang rejects or warns on
fails the pull request's Windows build rather than the nightly's Linux client, a
clean build on one platform is clean on the other, and `clang-tidy` on Windows
reads compile commands written for the compiler that builds the code. `clang-cl`
defines both `_MSC_VER` and `__clang__`; no check in our code takes either.
`clang-cl` was rejected while Windows built Falcor from source with its own
toolchain; with Falcor gone (ADR-0059) and the dependencies left on `cl`, none
of our code needs it.

The client (`augustac`, its Renderer and their tests) builds with the
`AUGUSTA_CLIENT` option, on by default on Windows and Linux, so
`cmake --preset linux` builds it as `cmake --preset windows` does. Its ports are
the vcpkg manifest's `client` feature, which the option installs: on Linux,
vcpkg's GLFW builds against the system's X11, Wayland and xkbcommon development
files, which `scripts/bootstrap/linux.sh` installs (and through it the dev
container), and loads the libraries themselves at runtime, choosing X11 or
Wayland by the session it finds. A build of the server alone turns the option
off: the server image, and every Linux CI job but the nightly's
`client-x64-linux`, so pull requests build the client on Windows only (ADR-0013)
and none of them needs the client's ports or system packages.

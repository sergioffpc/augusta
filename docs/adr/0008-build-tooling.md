# Build Tooling

The build uses CMake + Ninja + sccache, for cross-platform, fast, cached builds
across the client, on Windows and Linux, and the Linux server.

The default build (`all`) is the binaries alone. The tests, benchmarks and
fuzzers are excluded from it and build through one `augusta_tests` target, which
`make test` builds before `ctest` and CI builds wherever it runs them: building
a binary never waits on the test suite.

Beside it, each test area has an `augusta_tests_<area>` target that builds only
that area's test executables. An area is what an executable tests: a runtime
module's name, `client` or `server` for a Runtime's tests, or a tool's name for
the tests of a tool that has them. Each test executable names exactly one, as a
`ctest` label on its `gtest_discover_tests` call, and the targets are made from
those labels alone (`cmake/TestAreas.cmake`), so labelling an executable is all
that adding it to an area takes. `make test TAGS="<area> ..."` builds the union
of those areas' targets when every tag is an area, and `augusta_tests`
otherwise, then runs the tests carrying those labels: a change to one module
neither waits for nor runs the whole suite. The area check
(`checks/test_areas.cmake`, under `make check`) refuses an executable with no
area, with more than one, or with any label besides its area other than the
`check`, `property` and `netcode` that ADR-0013 allows.

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

Windows compiles our code and every vcpkg port with `clang-cl` and links them
with `lld-link`, from LLVM 21.1.8, the version that already gives `clang-format`
and `clang-tidy` on both platforms: one LLVM version to bump, `LLVM_VERSION` in
`scripts/bootstrap/versions.env`. `cmake/LlvmWindowsToolchain.cmake` names them,
with `llvm-lib`, `llvm-rc`, `llvm-mt` and `llvm-ml64`, and vcpkg's toolchain
chainloads it twice, as on Linux: from the windows presets for the project, and
from the overlay `triplets/x64-windows.cmake` for every port, whose build it
also gives vcpkg's own Windows toolchain, the CRT and MSVC flags vcpkg's stock
triplet builds with. vcpkg builds a port in an environment of its own, whose
PATH is not the user's, so the project's configure names the directory it found
`clang-cl` in (`AUGUSTA_LLVM_BIN`) and the triplet passes it through to the
ports. The Windows bootstrap installs that LLVM, and CI's and the release's
Windows client jobs install it through `.github/actions/setup-windows-llvm`.
They compile and link against the MSVC sysroot (the STL, the CRT, ATL and the
Windows SDK) that `make toolchain` links into `.toolchain/sysroot`, named with
`/winsysroot`, and no Visual Studio or developer environment is used (ADR-0079).
`clang-cl` sets CMake's `MSVC`, so the MSVC build settings apply to it
unchanged. Its `/W4 /WX` is clang's `-Wall -Wextra -Werror`, Linux's warning
set; `/Wall` would be `-Weverything`. The triplet otherwise keeps vcpkg's stock
`x64-windows` settings, its libraries dynamic, and builds `llvm` and `mesa`
release-only: Mesa is loaded only as lavapipe's Vulkan ICD, from its own DLL, so
a release Mesa serves a debug build with no two CRTs in one image, and LLVM
exists only to build Mesa. It gives PhysX the clang 21 warning suppressions
`triplets/x64-linux.cmake` does. The ports that built only with msbuild or
nmake, which ship with Visual Studio alone, are overlaid (`ports/`, ADR-0079).
vcpkg's ABI hash covers the compiler and the toolchain file, so the build that
moved the ports off MSVC `cl` rebuilt the whole Windows binary cache once. The
root project, the server, the client, their tests and their dependencies are
then compiled by one compiler family everywhere: code that clang rejects or
warns on fails the pull request's Windows build rather than the nightly's Linux
client, a clean build on one platform is clean on the other, and `clang-tidy` on
Windows reads compile commands written for the compiler that builds the code.
The tools' native modules (`tools/agent`, `tools/pack`) are built by the root
project too, so with `clang-cl` as well. `clang-cl` defines both `_MSC_VER` and
`__clang__`; no check in our code takes either. `clang-cl` was once rejected
because vcpkg's `x64-windows` triplet and NVIDIA's SDKs are built and tested
with MSVC, and the ports first stayed on `cl`. Neither holds: `clang-cl` builds
what `cl` did with the same STL and CRT, so it is ABI-compatible with NVIDIA's
prebuilt binaries (PhysX's GPU DLLs, Aftermath), which it links against as it
does any MSVC library.

macOS (arm64, a development build of the client, the server and their tests)
builds with Homebrew's LLVM 21 (`llvm@21`, `lld@21`), the version Linux and
Windows build with, not Apple's clang: clang compiles against LLVM's libc++,
linked dynamically from Homebrew's prefix as its LLVM documents, lld links, and
LLVM's binary tools stand in for Apple's; the Command Line Tools give the SDK
alone. `cmake/LlvmMacosToolchain.cmake` states it, chainloaded as on Linux, from
the macos presets and from `triplets/arm64-osx.cmake`, so its ports build with
it too. The nightly builds and tests it (`client-arm64-macos`, ADR-0013), and so
it gates every release.

clang is then the project's one compiler: `clang-cl` on Windows, LLVM's clang on
Linux and macOS. The root project refuses any other when it is configured - GCC,
MSVC's `cl`, Apple's clang (CMake's `AppleClang`) - rather than build with it by
accident, and the triplets give every vcpkg port the same clang.

Every target of the root project is built with its platform's standard binary
hardening, stated once beside the warning flags in the root `CMakeLists.txt`. On
Linux: `-fstack-protector-strong`, `-fstack-clash-protection`,
`-fcf-protection=full` on x86-64, full RELRO (`-z relro -z now`) over clang's
default PIE, and `-D_FORTIFY_SOURCE=3` in every build but Debug, which has no
optimisation for it to work with and which every sanitizer preset is. On
Windows: Control Flow Guard (`/guard:cf`) and CET shadow-stack compatibility
(`/CETCOMPAT`). Wherever the standard library is libc++ (Linux and macOS), its
fast hardening mode is on in every build: bounds and null checks of a compare
each. On Linux and Windows a test per executable reads back off `augustad` and
`augustac` what of it leaves a mark there: BIND_NOW, the stack protector and the
fortified functions; Control Flow Guard and CET compatibility
(`tests/binary_hardening.cmake`). The dependencies vcpkg builds keep their
triplets' flags, as none of these flags changes an ABI; lld then leaves the
Linux executables unmarked for CET, which needs every linked object marked.

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

# Build Tooling

The build uses CMake + Ninja + sccache, for cross-platform, fast, cached builds
across the Windows client and Linux server.

The default build (`all`) is the binaries alone. The tests, benchmarks and
fuzzers are excluded from it and build through one `augusta_tests` target, which
`make test` builds before `ctest` and CI builds wherever it runs them: building
a binary never waits on the test suite.

Linux (the server and the shared core, including the sanitizer and fuzzing
builds) compiles with clang, so libFuzzer, `llvm-cov`, and the sanitizers come
from one toolchain, the same one `clang-tidy` and `clang-format` already are.
Windows compiles with MSVC `cl`, which vcpkg's `x64-windows` triplet and
NVIDIA's SDKs are built and tested with; clang-cl was rejected for that reason
(see ENGINEERING.md, Developer Environment). The shared core therefore builds
with two compilers, which the NFR-03 golden trajectories compare (ADR-0013).

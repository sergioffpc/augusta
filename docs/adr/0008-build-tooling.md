# Build Tooling

The build uses CMake + Ninja + sccache, for cross-platform, fast, cached builds across the Windows client and Linux server.

Linux (the server and the shared core, including the sanitizer and fuzzing builds) compiles with clang, so libFuzzer, `llvm-cov`, and the sanitizers come from one toolchain, the same one `clang-tidy` and `clang-format` already are. Windows compiles with MSVC `cl`: Falcor supports only MSVC (ADR-0009), and clang-cl was rejected for that reason (see ENGINEERING.md, Developer Environment). The shared core therefore builds with two compilers, which the NFR-03 golden trajectories compare (ADR-0013).

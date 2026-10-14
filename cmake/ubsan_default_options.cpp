// UBSan's options for every binary of a UBSan build, which its runtime reads at
// start (the root CMakeLists.txt links this into each one). UBSAN_OPTIONS still
// adds to them.
//
// halt_on_error=1: UBSan otherwise prints its report and carries on, so a test
// that hits undefined behaviour exits 0 and passes (ADR-0013).
// print_stacktrace=1: the report then says where it came from, as ASan's does.

// NOLINTNEXTLINE(readability-identifier-naming) - the name UBSan's runtime looks up.
extern "C" __attribute__((used, visibility("default"))) const char* __ubsan_default_options() {
  return "halt_on_error=1:print_stacktrace=1";
}

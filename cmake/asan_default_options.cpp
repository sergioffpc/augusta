// ASan's options for every binary of an ASan build, which its runtime reads at
// start (the root CMakeLists.txt links this into each one). ASAN_OPTIONS still
// adds to them.
//
// detect_container_overflow=0: libc++ marks its containers' spare capacity for
// ASan, which holds only when every caller is instrumented. The vcpkg
// dependencies are not, so GameNetworkingSockets filling a vector it just
// resized, or GoogleTest destroying one, reads as an overflow (ADR-0008).

// NOLINTNEXTLINE(readability-identifier-naming) - the name ASan's runtime looks up.
extern "C" __attribute__((used, visibility("default"))) const char* __asan_default_options() {
  return "detect_container_overflow=0";
}

# BLAKE3 (pack hashing) and libsodium (Ed25519 signing/verification) back
# every pack's trailer (ADR-0030/ADR-0031) - needed by both the cooker
# (signs) and the client/server runtime (verifies), so unlike USD/
# DirectXTex/meshoptimizer (tools-only) these are base vcpkg.json
# dependencies, not gated behind a feature. Boost.Interprocess (portable
# mmap) is the same: Pack::Load memory-maps a pack once and every Resolve* call reads
# straight out of that mapping, on both the client/server (verify) and
# cooker (round-trip tests) sides.
find_package(blake3 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(boost_interprocess CONFIG REQUIRED)

add_library(augusta_assets STATIC
  assets.cpp
  encoder.cpp
  decoder.cpp
)
target_include_directories(augusta_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_assets PUBLIC augusta_math)
target_link_libraries(augusta_assets PRIVATE BLAKE3::blake3 unofficial-sodium::sodium Boost::interprocess)
augusta_set_module_scope(augusta_assets SHARED)

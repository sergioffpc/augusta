include(GoogleTest)

add_executable(augusta_version_tests version_test.cpp)
target_link_libraries(augusta_version_tests PRIVATE augusta_version GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_version_tests)

add_executable(augusta_version_benchmarks version_benchmark.cpp)
target_link_libraries(augusta_version_benchmarks PRIVATE augusta_version benchmark::benchmark benchmark::benchmark_main)

add_executable(augusta_networking_tests networking_test.cpp)
target_link_libraries(augusta_networking_tests PRIVATE augusta_networking GTest::gtest GTest::gtest_main)
# DISCOVERY_TIMEOUT above the 5s default: this binary pulls in GameNetworkingSockets,
# protobuf, and abseil as DLLs, and a fresh (especially Debug-config) build's first
# launch can take longer than 5s once Windows Defender scans the newly written
# binaries - CTest's discovery run then times out even though the test itself is fine.
gtest_discover_tests(augusta_networking_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_physics_tests physics_test.cpp)
target_link_libraries(augusta_physics_tests PRIVATE augusta_physics GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_physics_tests)

add_executable(augusta_prediction_tests prediction_test.cpp)
target_link_libraries(augusta_prediction_tests PRIVATE augusta_prediction GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_prediction_tests)

# Only when tools/asset-cooking is part of this configure (see the
# AUGUSTA_BUILD_ASSET_COOKING option in the top-level CMakeLists.txt) -
# off for a server-only build, which never adds that subdirectory.
if (TARGET augusta_asset_cooking_lib)
  add_executable(augusta_asset_pipeline_tests asset_pipeline_test.cpp)
  target_link_libraries(augusta_asset_pipeline_tests PRIVATE augusta_asset_cooking_lib augusta_assets GTest::gtest GTest::gtest_main)
  # The fixture stage lives next to this CMakeLists.txt rather than beside
  # the test binary, so the test needs its absolute source path baked in.
  target_compile_definitions(augusta_asset_pipeline_tests PRIVATE
    AUGUSTA_ASSET_PIPELINE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  )
  # DISCOVERY_TIMEOUT above the 5s default: this binary pulls in OpenUSD's
  # DLLs (like augusta_networking_tests above pulls in GameNetworkingSockets'),
  # and a fresh build's first launch can take longer than 5s once Windows
  # Defender scans the newly written binaries.
  gtest_discover_tests(augusta_asset_pipeline_tests DISCOVERY_TIMEOUT 30)
endif()

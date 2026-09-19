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

add_executable(augusta_assets_tests assets_test.cpp)
target_link_libraries(augusta_assets_tests PRIVATE augusta_assets GTest::gtest GTest::gtest_main)
# WritePack/Encode*/Pack::Load round-trip coverage (below) exercises
# augusta_assets' own private pack-format internals directly (encoder.h/
# decoder.h, not under include/augusta/ - see that header's own comment
# on why: the pack-cooking pipeline, tools/pack, reimplements
# this wire format in pure Python instead of linking against it). This
# target gets a private include path to see them; nothing else needs to.
target_include_directories(augusta_assets_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
gtest_discover_tests(augusta_assets_tests)

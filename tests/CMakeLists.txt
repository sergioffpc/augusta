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

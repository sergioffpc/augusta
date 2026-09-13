include(GoogleTest)

add_executable(augusta_version_tests version_test.cpp)
target_link_libraries(augusta_version_tests PRIVATE augusta_version GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_version_tests)

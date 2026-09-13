include(GoogleTest)

add_executable(augusta_shared_tests engine_version_test.cpp)
target_link_libraries(augusta_shared_tests PRIVATE augusta_shared GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_shared_tests)

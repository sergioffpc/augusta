include(GoogleTest)

# The C++ tests' load test (see match_loop.h): Agents composing Intents as
# load_test.py does, with no Python. Untagged, as a test executable is: it
# holds Agents and the test-only server in one process.
add_library(augusta_agent_match_loop STATIC match_loop.cpp)
target_include_directories(augusta_agent_match_loop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_agent_match_loop
  PUBLIC augusta_agent_core augusta_harness
  PRIVATE augusta_gameplay augusta_math augusta_physics
)

# Whole runs of Agents against a host::Host in the same process, through the
# example scenario's packs and Game policy.
add_executable(augusta_agent_run_tests run_test.cpp)
target_link_libraries(
  augusta_agent_run_tests
  PRIVATE augusta_agent_match_loop augusta_agent_testing_server augusta_networking GTest::gtest GTest::gtest_main
)
target_compile_definitions(
  augusta_agent_run_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links PhysX through augusta_physics, whose first launch can outlast the
# default discovery timeout (see tests/CMakeLists.txt).
gtest_discover_tests(augusta_agent_run_tests DISCOVERY_TIMEOUT 30)

# The netcode under an impaired link: whole runs under the transport's simulated
# latency, jitter, loss and reordering, minutes each, so they carry the label
# netcode, which every test preset leaves out but linux-netcode (the
# nightly's), which runs them alone.
add_executable(augusta_agent_netcode_tests netcode_test.cpp)
target_link_libraries(
  augusta_agent_netcode_tests
  PRIVATE
    augusta_agent_match_loop
    augusta_agent_testing_server
    augusta_harness
    augusta_networking
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(
  augusta_agent_netcode_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
gtest_discover_tests(augusta_agent_netcode_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS netcode TIMEOUT 900)

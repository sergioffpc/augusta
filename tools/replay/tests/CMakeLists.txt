include(GoogleTest)

# The replay (ADR-0048), on recordings of the example scenario made in the test
# (scripted_match.h), and the golden match recorded from it, which guards the
# simulation's determinism across builds (ADR-0013). When the simulation changes on purpose, one step rewrites
# the golden match for review and commit:
#   cmake --build --preset <preset> --target augusta_golden_match
add_executable(augusta_replay_tests replay_test.cpp)
target_link_libraries(
  augusta_replay_tests
  PRIVATE augusta_replay augustad_content augustad_host augusta_logging augusta_version GTest::gtest GTest::gtest_main
)
target_compile_definitions(
  augusta_replay_tests
  PRIVATE
    AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
    AUGUSTA_GOLDEN_MATCH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden_match.rec"
)
# Links PhysX, whose first launch can outlast the default discovery timeout
# (see tests/CMakeLists.txt).
gtest_discover_tests(augusta_replay_tests DISCOVERY_TIMEOUT 30)
add_custom_target(
  augusta_golden_match
  COMMAND
    augusta_replay_tests --gtest_also_run_disabled_tests --gtest_filter=GoldenMatchTest.DISABLED_RegenerateGoldenMatch
  COMMENT "Rewriting tools/replay/tests/fixtures/golden_match.rec"
  VERBATIM
)

# What a real Host recorded, with real clients connected over loopback,
# replays to the same outcome.
add_executable(augusta_host_replay_tests host_replay_test.cpp)
target_link_libraries(
  augusta_host_replay_tests
  PRIVATE
    augusta_replay
    augustad_content
    augustad_host
    augusta_harness
    augusta_networking
    augusta_parameters
    augusta_physics
    augusta_prediction
    GTest::gtest
    GTest::gtest_main
)
# Links networking and PhysX (see above).
gtest_discover_tests(augusta_host_replay_tests DISCOVERY_TIMEOUT 30)

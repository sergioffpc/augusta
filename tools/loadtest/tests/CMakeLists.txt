include(GoogleTest)

# What a Scripted player decides each tick, from Server views built by hand.
add_executable(augusta_loadtest_scripted_player_tests scripted_player_test.cpp)
target_link_libraries(augusta_loadtest_scripted_player_tests PRIVATE
  augusta_loadtest_scripted_player augusta_input augusta_math GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, whose first launch can outlast the
# default discovery timeout (see tests/CMakeLists.txt).
gtest_discover_tests(augusta_loadtest_scripted_player_tests DISCOVERY_TIMEOUT 30)

# What a Scripted player's ticks add up to, from Prediction States built by hand.
add_executable(augusta_loadtest_netcode_stats_tests netcode_stats_test.cpp)
target_link_libraries(augusta_loadtest_netcode_stats_tests PRIVATE
  augusta_loadtest_netcode_stats augusta_math augusta_prediction GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_prediction (see above).
gtest_discover_tests(augusta_loadtest_netcode_stats_tests DISCOVERY_TIMEOUT 30)

# Whole runs of Scripted players against a server::Host in the same process,
# one of them through the example scenario's packs and Game policy.
add_executable(augusta_loadtest_run_tests run_test.cpp)
target_link_libraries(augusta_loadtest_run_tests PRIVATE
  augusta_loadtest_run augustad_content augustad_host augusta_assets augusta_map augusta_networking augusta_parameters
  GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_loadtest_run_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
gtest_discover_tests(augusta_loadtest_run_tests DISCOVERY_TIMEOUT 30)

# The settings file's keys, and the example a new setup is copied from.
add_executable(augusta_loadtest_settings_tests settings_test.cpp)
target_link_libraries(augusta_loadtest_settings_tests PRIVATE augusta_loadtest_settings augusta_config GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_loadtest_settings_tests PRIVATE
  AUGUSTA_LOADTEST_EXAMPLE_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/../augusta-loadtest.example.yaml")
gtest_discover_tests(augusta_loadtest_settings_tests DISCOVERY_TIMEOUT 30)

# The netcode under an impaired link: whole runs under the transport's simulated
# latency, jitter, loss and reordering, minutes each, so they carry the label
# netcode, which the tools test presets leave out and the linux-netcode one
# (the nightly's) runs alone.
add_executable(augusta_loadtest_netcode_tests netcode_test.cpp)
target_link_libraries(augusta_loadtest_netcode_tests PRIVATE
  augusta_loadtest_run augusta_loadtest_netcode_stats augustad_content augustad_host augusta_assets augusta_map
  augusta_networking augusta_parameters GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_loadtest_netcode_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
gtest_discover_tests(augusta_loadtest_netcode_tests DISCOVERY_TIMEOUT 30
  PROPERTIES LABELS netcode TIMEOUT 900)

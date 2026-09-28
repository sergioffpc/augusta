include(GoogleTest)

add_executable(augusta_version_tests version_test.cpp)
target_link_libraries(augusta_version_tests PRIVATE augusta_version GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_version_tests)

add_executable(augusta_logging_tests logging_test.cpp)
target_link_libraries(augusta_logging_tests PRIVATE augusta_logging GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_logging_tests)

add_executable(augusta_version_benchmarks version_benchmark.cpp)
target_link_libraries(augusta_version_benchmarks PRIVATE augusta_version benchmark::benchmark benchmark::benchmark_main)

add_executable(augusta_tick_tests tick_test.cpp)
target_link_libraries(augusta_tick_tests PRIVATE augusta_tick GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_tick_tests)

add_executable(augusta_config_tests config_test.cpp)
target_link_libraries(augusta_config_tests PRIVATE augusta_config GTest::gtest GTest::gtest_main)
# The example configs are what a new install is copied from, so a test keeps them loading.
target_compile_definitions(augusta_config_tests PRIVATE
  AUGUSTA_EXAMPLE_CLIENT_CONFIG="${CMAKE_SOURCE_DIR}/config/augustac.example.yaml"
  AUGUSTA_EXAMPLE_SERVER_CONFIG="${CMAKE_SOURCE_DIR}/config/augustad.example.yaml")
# Links PhysX through augusta_input, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_config_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_parameters_tests parameters_test.cpp)
target_link_libraries(augusta_parameters_tests PRIVATE augusta_parameters augusta_protocol GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_parameters_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_parameters_loader_tests parameters_loader_test.cpp)
target_link_libraries(augusta_parameters_loader_tests PRIVATE augustad_parameters_loader GTest::gtest GTest::gtest_main)
# The example scenario's script is what a new scenario is copied from, so a
# test keeps it loading.
target_compile_definitions(augusta_parameters_loader_tests PRIVATE
  AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/pack/examples/authoring/scenarios/augusta/parameters.lua")
gtest_discover_tests(augusta_parameters_loader_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_protocol_tests protocol_test.cpp)
target_link_libraries(augusta_protocol_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_protocol_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_protocol_property_tests protocol_property_test.cpp)
target_link_libraries(augusta_protocol_property_tests PRIVATE augusta_protocol rapidcheck_gtest GTest::gtest GTest::gtest_main)
# RapidCheck's generator templates, one per message field, outgrow MSVC's
# default section count for one object file.
target_compile_options(augusta_protocol_property_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23. Clang
# reports it where our code instantiates the template, so its system include
# path does not silence it; nothing of ours is deprecated in this target.
target_compile_options(augusta_protocol_property_tests PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
# The property label is how the nightly selects the property tests to run at
# a high case count (ADR-0013).
gtest_discover_tests(augusta_protocol_property_tests PROPERTIES LABELS property)

add_executable(augusta_simulation_property_tests simulation_property_test.cpp)
target_link_libraries(augusta_simulation_property_tests PRIVATE
  augusta_simulation augusta_prediction augusta_input augusta_logging rapidcheck_gtest GTest::gtest GTest::gtest_main)
# The same RapidCheck deprecation as augusta_protocol_property_tests above.
target_compile_options(augusta_simulation_property_tests PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_property_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS property)

add_executable(augusta_match_tests match_test.cpp)
target_link_libraries(augusta_match_tests PRIVATE augustad_match GTest::gtest GTest::gtest_main)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_match_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_command_queue_tests command_queue_test.cpp)
target_link_libraries(augusta_command_queue_tests PRIVATE augustad_commands GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_command, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_command_queue_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_replication_tests replication_test.cpp)
target_link_libraries(augusta_replication_tests PRIVATE augusta_replication GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_simulation, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_replication_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_simulation_tests simulation_test.cpp)
target_link_libraries(augusta_simulation_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_networking_tests networking_test.cpp)
target_link_libraries(augusta_networking_tests PRIVATE augusta_networking GTest::gtest GTest::gtest_main)
# DISCOVERY_TIMEOUT above the 5s default: this binary pulls in GameNetworkingSockets,
# protobuf, and abseil as DLLs, and a fresh (especially Debug-config) build's first
# launch can take longer than 5s once Windows Defender scans the newly written
# binaries - CTest's discovery run then times out even though the test itself is fine.
gtest_discover_tests(augusta_networking_tests DISCOVERY_TIMEOUT 30)

# Which flags a send goes out with is a header private to the networking
# module, so it is tested apart from a live connection through a private
# include path; GameNetworkingSockets is linked only for its flag constants.
add_executable(augusta_send_flags_tests send_flags_test.cpp)
target_include_directories(augusta_send_flags_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/networking)
target_link_libraries(augusta_send_flags_tests PRIVATE
  augusta_networking
  $<IF:$<TARGET_EXISTS:GameNetworkingSockets::GameNetworkingSockets>,GameNetworkingSockets::GameNetworkingSockets,GameNetworkingSockets::GameNetworkingSockets_s>
  GTest::gtest GTest::gtest_main)
# Links GameNetworkingSockets, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_send_flags_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_physics_tests physics_test.cpp)
target_link_libraries(augusta_physics_tests PRIVATE augusta_physics GTest::gtest GTest::gtest_main)
# DISCOVERY_TIMEOUT above the 5s default, same reason as the networking tests
# above: the PhysX DLLs make a fresh build's first launch slow under Windows Defender.
gtest_discover_tests(augusta_physics_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_ballistics_tests ballistics_test.cpp)
target_link_libraries(augusta_ballistics_tests PRIVATE augusta_ballistics GTest::gtest GTest::gtest_main)
# NFR-03's golden trajectories (ADR-0013). When the ballistics model changes on
# purpose, one step rewrites them for review and commit:
#   cmake --build --preset <preset> --target augusta_golden_trajectories
target_compile_definitions(augusta_ballistics_tests PRIVATE
  AUGUSTA_GOLDEN_TRAJECTORIES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden_trajectories.txt")
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_ballistics_tests DISCOVERY_TIMEOUT 30)
add_custom_target(augusta_golden_trajectories
  COMMAND augusta_ballistics_tests --gtest_also_run_disabled_tests
    --gtest_filter=BallisticsGoldenTest.DISABLED_RegenerateGoldenFile
  COMMENT "Rewriting tests/fixtures/golden_trajectories.txt"
  VERBATIM)

add_executable(augusta_reconciliation_tests reconciliation_test.cpp)
target_link_libraries(augusta_reconciliation_tests PRIVATE augusta_prediction GTest::gtest GTest::gtest_main)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_reconciliation_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_correction_tests correction_test.cpp)
target_link_libraries(augusta_correction_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_correction_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_input_tests input_test.cpp)
target_link_libraries(augusta_input_tests PRIVATE augusta_input GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_input_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_interpolation_tests interpolation_test.cpp)
target_link_libraries(augusta_interpolation_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_interpolation_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_prediction_tests prediction_test.cpp)
target_link_libraries(augusta_prediction_tests PRIVATE augusta_prediction GTest::gtest GTest::gtest_main)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_prediction_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_map_tests map_test.cpp)
target_link_libraries(augusta_map_tests PRIVATE augusta_map GTest::gtest GTest::gtest_main)
# Builds real packs with augusta_assets' private encoder (see augusta_assets_tests below).
target_include_directories(augusta_map_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_map_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_session_tests session_test.cpp)
target_link_libraries(augusta_session_tests PRIVATE augusta_harness augustad_host augustad_parameters_loader GTest::gtest GTest::gtest_main)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_session_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_wire_tests wire_test.cpp)
target_link_libraries(augusta_wire_tests PRIVATE augusta_harness augustad_host GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_wire_tests DISCOVERY_TIMEOUT 30)

# No header past the protocol's edge names a protocol type (ADR-0038): only
# harness_wire.h and server/wire.h convert. Nor does a presentation header name
# the harness: ClientRuntime converts. A source scan, so it builds nothing.
add_test(NAME augusta_protocol_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/protocol_boundary.cmake)

# Neither physics nor SimulationWorld names the protocol or the client's input
# sampler in its public headers: the grids and the Command they need are shared
# core modules of their own. A source scan, so it builds nothing.
add_test(NAME augusta_core_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/core_boundary.cmake)

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

# Loads the golden packs the Python cooker wrote from its example scenario
# (ADR-0013); the cooker's own tests keep it writing them byte for byte.
add_executable(augusta_cooked_pack_tests cooked_pack_test.cpp)
target_link_libraries(augusta_cooked_pack_tests PRIVATE augusta_assets GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_cooked_pack_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
gtest_discover_tests(augusta_cooked_pack_tests)

# The renderer is Windows-only (Falcor, ADR-0009), but which region of its
# per-frame upload buffers a frame uses is a Falcor-free header, private to the
# module, so it is tested everywhere through a private include path.
add_executable(augusta_frame_regions_tests frame_regions_test.cpp)
target_include_directories(augusta_frame_regions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(augusta_frame_regions_tests PRIVATE GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_frame_regions_tests)

if (WIN32)
  # The client is Windows-only (Falcor, ADR-0009), and so is the scene
  # loader's target.
  add_executable(augusta_client_scene_tests client_scene_test.cpp)
  target_link_libraries(augusta_client_scene_tests PRIVATE augustac_scene GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_scene_tests)
endif()

add_subdirectory(fuzz)

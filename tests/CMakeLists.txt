include(GoogleTest)

add_executable(augusta_version_tests version_test.cpp)
target_link_libraries(augusta_version_tests PRIVATE augusta_version GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_version_tests)

add_executable(augusta_logging_tests logging_test.cpp)
target_link_libraries(augusta_logging_tests PRIVATE augusta_logging GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_logging_tests)

add_executable(augusta_version_benchmarks version_benchmark.cpp)
target_link_libraries(augusta_version_benchmarks PRIVATE augusta_version benchmark::benchmark benchmark::benchmark_main)

# The hot paths the nightly times and keeps the history of (ADR-0013): one
# binary, so one run gives one result file for the whole set.
add_executable(augusta_benchmarks
  ballistics_benchmark.cpp
  pack_benchmark.cpp
  protocol_benchmark.cpp
  simulation_benchmark.cpp)
# The tick runs the example scenario's Game policy, as the server loads it from
# its golden server pack.
target_link_libraries(augusta_benchmarks PRIVATE
  augusta_assets augusta_ballistics augusta_protocol augusta_simulation augustad_policy_loader
  benchmark::benchmark benchmark::benchmark_main)
target_compile_definitions(augusta_benchmarks PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")

add_executable(augusta_tick_tests tick_test.cpp)
target_link_libraries(augusta_tick_tests PRIVATE augusta_tick GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_tick_tests)

add_executable(augusta_supervisor_tests supervisor_test.cpp)
target_link_libraries(augusta_supervisor_tests PRIVATE augusta_supervisor GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_supervisor_tests)

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
  AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scenarios/augusta/parameters.lua")
gtest_discover_tests(augusta_parameters_loader_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_protocol_tests protocol_test.cpp)
target_link_libraries(augusta_protocol_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_protocol_tests DISCOVERY_TIMEOUT 30)

# A match recording's records, in the protocol's encoding (ADR-0048).
add_executable(augusta_protocol_recording_tests protocol_recording_test.cpp)
target_link_libraries(augusta_protocol_recording_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_protocol_recording_tests DISCOVERY_TIMEOUT 30)

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

add_executable(augusta_misbehaviour_tests misbehaviour_test.cpp)
target_link_libraries(augusta_misbehaviour_tests PRIVATE augustad_misbehaviour GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_misbehaviour_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_heartbeat_tests heartbeat_test.cpp)
target_link_libraries(augusta_heartbeat_tests PRIVATE augustad_heartbeat GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_heartbeat_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_admission_tests admission_test.cpp)
target_link_libraries(augusta_admission_tests PRIVATE augustad_admission GTest::gtest GTest::gtest_main)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_admission_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_replication_tests replication_test.cpp)
target_link_libraries(augusta_replication_tests PRIVATE augusta_replication GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_simulation, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_replication_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_simulation_tests simulation_test.cpp)
target_link_libraries(augusta_simulation_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_tests DISCOVERY_TIMEOUT 30)

# SimulationWorld with a Game policy script the test chooses (ADR-0022).
add_executable(augusta_policy_tests policy_test.cpp)
target_link_libraries(augusta_policy_tests PRIVATE
  augusta_simulation augusta_scripting augustad_parameters_loader augustad_policy_loader augusta_logging
  GTest::gtest GTest::gtest_main)
# A policy script shares no globals with the example scenario's Parameters
# script, and the example's own policy scripts run as the server loads them
# from its golden server pack (ADR-0013).
target_compile_definitions(augusta_policy_tests PRIVATE
  AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scenarios/augusta/parameters.lua"
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_policy_tests DISCOVERY_TIMEOUT 30)

# What a hook's answer becomes once C++ validates it, apart from any script.
add_executable(augusta_policy_actions_tests policy_actions_test.cpp)
target_link_libraries(augusta_policy_actions_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_simulation, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_policy_actions_tests DISCOVERY_TIMEOUT 30)

# The example scenario's shipped objectives.lua, loaded out of a pack built with
# augusta_assets' private encoder and run through SimulationWorld (ADR-0013).
add_executable(augusta_example_objectives_tests example_objectives_test.cpp)
target_link_libraries(augusta_example_objectives_tests PRIVATE
  augusta_simulation augustad_policy_loader augusta_logging GTest::gtest GTest::gtest_main)
target_include_directories(augusta_example_objectives_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
target_compile_definitions(augusta_example_objectives_tests PRIVATE
  AUGUSTA_EXAMPLE_SCENARIO="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scenarios/augusta")
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_example_objectives_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_networking_tests networking_test.cpp)
target_link_libraries(augusta_networking_tests PRIVATE augusta_networking GTest::gtest GTest::gtest_main
  $<$<PLATFORM_ID:Windows>:ws2_32>)
# The plain sockets it holds ports with bring in <windows.h> on Windows.
target_compile_definitions(augusta_networking_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
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

# What a transport event does to the peers is decided in a header private to
# the networking module, tested apart from a live connection the same way.
add_executable(augusta_transport_events_tests transport_events_test.cpp)
target_include_directories(augusta_transport_events_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/networking)
target_link_libraries(augusta_transport_events_tests PRIVATE augusta_networking GTest::gtest GTest::gtest_main)
# Links GameNetworkingSockets, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_transport_events_tests DISCOVERY_TIMEOUT 30)

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

add_executable(augusta_local_view_tests local_view_test.cpp)
target_link_libraries(augusta_local_view_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_local_view_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_tracers_tests tracers_test.cpp)
target_link_libraries(augusta_tracers_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_tracers_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_effects_tests effects_test.cpp)
target_link_libraries(augusta_effects_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_effects_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_audio_cues_tests audio_cues_test.cpp)
target_link_libraries(augusta_audio_cues_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_audio_cues_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_prediction_tests prediction_test.cpp)
# Links augusta_presentation too: a new Match must not show as a correction.
target_link_libraries(augusta_prediction_tests PRIVATE augusta_prediction augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_prediction_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_map_tests map_test.cpp)
target_link_libraries(augusta_map_tests PRIVATE augusta_map GTest::gtest GTest::gtest_main)
# Builds real packs with augusta_assets' private encoder (see augusta_assets_tests below).
target_include_directories(augusta_map_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_map_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_policy_loader_tests policy_loader_test.cpp)
target_link_libraries(augusta_policy_loader_tests PRIVATE augustad_policy_loader GTest::gtest GTest::gtest_main)
# Builds real packs with augusta_assets' private encoder (see augusta_assets_tests below).
target_include_directories(augusta_policy_loader_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
gtest_discover_tests(augusta_policy_loader_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_session_tests session_test.cpp)
target_link_libraries(augusta_session_tests PRIVATE
  augusta_harness augustad_host augustad_parameters_loader augustad_policy_loader augusta_logging
  GTest::gtest GTest::gtest_main)
# The server runs the example scenario's Game policy, as it loads it from its
# golden server pack: the Spawn points and the win condition (US-03, US-14).
target_compile_definitions(augusta_session_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_session_tests DISCOVERY_TIMEOUT 30)

# Match recordings (ADR-0048), on the example scenario as the server loads it
# from its golden server pack. Their replay is tools/replay's, tested there.
add_executable(augusta_recording_tests recording_test.cpp)
target_link_libraries(augusta_recording_tests PRIVATE augustad_host augustad_content augusta_logging augusta_version
  GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_recording_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
# Links PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_recording_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_runner_tests runner_test.cpp)
target_link_libraries(augusta_runner_tests PRIVATE augusta_harness augustad_host GTest::gtest GTest::gtest_main)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_runner_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_wire_tests wire_test.cpp)
target_link_libraries(augusta_wire_tests PRIVATE augusta_harness augustad_host GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_wire_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_simulation_mapping_tests simulation_mapping_test.cpp)
target_link_libraries(augusta_simulation_mapping_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_simulation_mapping_tests DISCOVERY_TIMEOUT 30)

# No header past the protocol's edge names a protocol type (ADR-0038): only
# harness_wire.h and server/wire.h convert. Nor does a presentation header name
# the harness: the client converts (src/client/frame_mapping.h). A source scan, so it builds nothing.
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

# The client's cue sounds; builds real packs with augusta_assets' private
# encoder, as augusta_assets_tests does.
add_executable(augusta_cues_tests cues_test.cpp)
target_link_libraries(augusta_cues_tests PRIVATE augusta_assets augusta_cues GTest::gtest GTest::gtest_main)
target_include_directories(augusta_cues_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
gtest_discover_tests(augusta_cues_tests)

# Loads the golden packs the Python cooker wrote from its example scenario
# (ADR-0013); the cooker's own tests keep it writing them byte for byte.
add_executable(augusta_cooked_pack_tests cooked_pack_test.cpp)
# Also loads the example's Game policy scripts as the server does (ADR-0022),
# and every cue's sound as the client does.
target_link_libraries(augusta_cooked_pack_tests PRIVATE augusta_assets augusta_cues augustad_policy_loader GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_cooked_pack_tests PRIVATE
  AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs")
gtest_discover_tests(augusta_cooked_pack_tests)

# The crash lines' format, against the log sink's, and the handler itself in
# death tests.
add_executable(augusta_crash_tests crash_test.cpp)
target_link_libraries(augusta_crash_tests PRIVATE augustad_crash augusta_logging GTest::gtest GTest::gtest_main)
target_compile_definitions(augusta_crash_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
gtest_discover_tests(augusta_crash_tests)

# The crash lines' timestamp against the log sink's over every second to the
# year 9999.
add_executable(augusta_crash_property_tests crash_property_test.cpp)
target_link_libraries(augusta_crash_property_tests PRIVATE augustad_crash augusta_logging rapidcheck_gtest GTest::gtest GTest::gtest_main)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23 (see
# augusta_protocol_property_tests).
target_compile_options(augusta_crash_property_tests PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
gtest_discover_tests(augusta_crash_property_tests PROPERTIES LABELS property)

# The renderer is Windows-only (Falcor, ADR-0009), but which region of its
# per-frame upload buffers a frame uses is a Falcor-free header, private to the
# module, so it is tested everywhere through a private include path.
add_executable(augusta_frame_regions_tests frame_regions_test.cpp)
target_include_directories(augusta_frame_regions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(augusta_frame_regions_tests PRIVATE GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_frame_regions_tests)

if (WIN32)
  # The client is Windows-only (Falcor, ADR-0009), and so are its private
  # libraries' targets - though none of these links Falcor.
  add_executable(augusta_client_scene_tests client_scene_test.cpp)
  target_link_libraries(augusta_client_scene_tests PRIVATE augustac_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_scene_tests)

  add_executable(augusta_client_character_tests client_character_test.cpp)
  target_link_libraries(augusta_client_character_tests PRIVATE augustac_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_character_tests)

  add_executable(augusta_client_frame_mapping_tests client_frame_mapping_test.cpp)
  target_link_libraries(augusta_client_frame_mapping_tests PRIVATE augustac_frame_mapping GTest::gtest GTest::gtest_main)
  # Links networking and PhysX through the harness, so the same first-launch
  # cost applies as above.
  gtest_discover_tests(augusta_client_frame_mapping_tests DISCOVERY_TIMEOUT 30)

  add_executable(augusta_client_lobby_readiness_tests client_lobby_readiness_test.cpp)
  target_link_libraries(augusta_client_lobby_readiness_tests PRIVATE augustac_lobby_readiness GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_lobby_readiness_tests DISCOVERY_TIMEOUT 30)

  add_executable(augusta_client_net_stats_tests client_net_stats_test.cpp)
  target_link_libraries(augusta_client_net_stats_tests PRIVATE augustac_net_stats GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_net_stats_tests DISCOVERY_TIMEOUT 30)
endif()

add_subdirectory(fuzz)

# Every executable here and in fuzz/, in tools/replay/tests (the tests of
# augusta-replay, kept next to it), and in tools/swarm/tests (the tests of
# augusta-swarm) when AUGUSTA_SWARM builds the tool:
# what ctest, the benchmarks and the fuzz jobs run. Each directory is
# EXCLUDE_FROM_ALL, so this is how they get built. Custom targets (the seed
# regenerators) stay out: they rewrite committed files.
add_custom_target(augusta_tests)
set(test_directories ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz ${CMAKE_SOURCE_DIR}/tools/replay/tests)
if (AUGUSTA_SWARM)
  list(APPEND test_directories ${CMAKE_SOURCE_DIR}/tools/swarm/tests)
endif()
foreach(directory IN LISTS test_directories)
  get_property(directory_targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS directory_targets)
    get_target_property(target_type ${target} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
      add_dependencies(augusta_tests ${target})
    endif()
  endforeach()
endforeach()

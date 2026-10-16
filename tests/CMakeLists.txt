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
add_executable(
  augusta_benchmarks
  ballistics_benchmark.cpp
  interpolation_benchmark.cpp
  pack_benchmark.cpp
  presentation_benchmark.cpp
  protocol_benchmark.cpp
  replay_benchmark.cpp
  replication_benchmark.cpp
  simulation_benchmark.cpp
)
# The tick runs the example scenario's Game policy, as the server loads it from
# its golden server pack; replication is server::Host's own
# (augustad_host), and a render frame plays the golden client pack's cue sounds.
target_link_libraries(
  augusta_benchmarks
  PRIVATE
    augusta_assets
    augusta_audio
    augusta_cues
    augusta_gameplay
    augusta_presentation
    augusta_primitives
    augusta_protocol
    augusta_simulation
    augusta_version
    augustad_host
    augustad_policy_loader
    benchmark::benchmark
    benchmark::benchmark_main
)
target_compile_definitions(
  augusta_benchmarks
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)

# Links augusta_primitives alone, so the build fails if its header ever needs
# another module's include path.
add_executable(augusta_primitives_tests primitives_test.cpp)
target_link_libraries(augusta_primitives_tests PRIVATE augusta_primitives GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_primitives_tests)

add_executable(augusta_tick_tests tick_test.cpp)
target_link_libraries(augusta_tick_tests PRIVATE augusta_gameplay GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_tick_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_supervisor_tests supervisor_test.cpp)
target_link_libraries(augusta_supervisor_tests PRIVATE augusta_failure augusta_lifecycle GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_supervisor_tests)

add_executable(augusta_failure_tests failure_test.cpp)
target_link_libraries(augusta_failure_tests PRIVATE augusta_failure GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_failure_tests)

add_executable(augusta_application_tests application_test.cpp)
target_link_libraries(
  augusta_application_tests
  PRIVATE augusta_failure augusta_lifecycle augusta_logging GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_application_tests)

add_executable(augusta_config_tests config_test.cpp)
target_link_libraries(augusta_config_tests PRIVATE augusta_config GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_config_tests)

# The example configs are what a new install is copied from, so a test keeps each loading.
add_executable(augusta_client_config_tests client_config_test.cpp)
target_link_libraries(augusta_client_config_tests PRIVATE augusta_client_config GTest::gtest GTest::gtest_main)
target_compile_definitions(
  augusta_client_config_tests
  PRIVATE AUGUSTA_EXAMPLE_CLIENT_CONFIG="${CMAKE_SOURCE_DIR}/config/augustac.example.yaml"
)
# Links PhysX through augusta_input, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_client_config_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_server_config_tests server_config_test.cpp)
target_link_libraries(augusta_server_config_tests PRIVATE augusta_server_config GTest::gtest GTest::gtest_main)
target_compile_definitions(
  augusta_server_config_tests
  PRIVATE AUGUSTA_EXAMPLE_SERVER_CONFIG="${CMAKE_SOURCE_DIR}/config/augustad.example.yaml"
)
gtest_discover_tests(augusta_server_config_tests)

add_executable(augusta_parameters_tests parameters_test.cpp)
target_link_libraries(
  augusta_parameters_tests
  PRIVATE augusta_gameplay augusta_primitives GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_parameters_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_parameters_loader_tests parameters_loader_test.cpp)
target_link_libraries(augusta_parameters_loader_tests PRIVATE augustad_parameters_loader GTest::gtest GTest::gtest_main)
# The example scenario's script is what a new scenario is copied from, so a
# test keeps it loading.
target_compile_definitions(
  augusta_parameters_loader_tests
  PRIVATE
    AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scripts/parameters/rules_of_engagement.lua"
)
gtest_discover_tests(augusta_parameters_loader_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_protocol_tests protocol_test.cpp)
target_link_libraries(augusta_protocol_tests PRIVATE augusta_primitives augusta_protocol GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_protocol_tests DISCOVERY_TIMEOUT 30)

# A Match capture's records, in the protocol's encoding (ADR-0050).
add_executable(augusta_protocol_capture_tests protocol_capture_test.cpp)
target_link_libraries(augusta_protocol_capture_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_protocol_capture_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_protocol_property_tests protocol_property_test.cpp)
target_link_libraries(
  augusta_protocol_property_tests
  PRIVATE augusta_primitives augusta_protocol rapidcheck_gtest GTest::gtest GTest::gtest_main
)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23. Clang
# reports it where our code instantiates the template, so its system include
# path does not silence it; nothing of ours is deprecated in this target.
target_compile_options(augusta_protocol_property_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
# The property label is how the nightly selects the property tests to run at
# a high case count (ADR-0013).
gtest_discover_tests(augusta_protocol_property_tests PROPERTIES LABELS property)

add_executable(augusta_simulation_property_tests simulation_property_test.cpp)
target_link_libraries(
  augusta_simulation_property_tests
  PRIVATE
    augusta_simulation
    augusta_prediction
    augusta_input
    augusta_logging
    rapidcheck_gtest
    GTest::gtest
    GTest::gtest_main
)
# The same RapidCheck deprecation as augusta_protocol_property_tests above.
target_compile_options(
  augusta_simulation_property_tests
  PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_property_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS property)

add_executable(augusta_match_tests match_test.cpp)
target_link_libraries(augusta_match_tests PRIVATE augustad_match GTest::gtest GTest::gtest_main)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_match_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_command_queue_tests command_queue_test.cpp)
target_link_libraries(augusta_command_queue_tests PRIVATE augustad_commands GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_command_queue_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_misbehaviour_tests misbehaviour_test.cpp)
target_link_libraries(augusta_misbehaviour_tests PRIVATE augustad_misbehaviour GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_misbehaviour_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_heartbeat_tests heartbeat_test.cpp)
target_link_libraries(augusta_heartbeat_tests PRIVATE augustad_heartbeat GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_heartbeat_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_liveness_tests liveness_test.cpp)
target_link_libraries(augusta_liveness_tests PRIVATE augustad_liveness GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_liveness_tests DISCOVERY_TIMEOUT 30)

# Over real HTTP on the loopback, on a port the test finds free, with the
# acceptor's failures injected (see faults.h) and the log line they end in read
# off stdout.
find_package(boost_beast CONFIG REQUIRED)
add_executable(augusta_metrics_tests metrics_test.cpp)
target_link_libraries(
  augusta_metrics_tests
  PRIVATE augustad_metrics augustad_liveness augusta_failure augusta_logging Boost::beast GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_metrics_tests DISCOVERY_TIMEOUT 30)

# What the Host counts, collected as the endpoint collects it, without HTTP.
add_executable(augusta_host_metrics_tests host_metrics_test.cpp)
target_link_libraries(augusta_host_metrics_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
# Links networking and PhysX through augustad_host, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_host_metrics_tests DISCOVERY_TIMEOUT 30)

# Reads what it records back as the metrics endpoint collects it.
add_executable(augusta_connection_health_tests connection_health_test.cpp)
target_link_libraries(augusta_connection_health_tests PRIVATE augustad_connection_health GTest::gtest GTest::gtest_main)
# Links networking (for ConnectionStats), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_connection_health_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_admission_tests admission_test.cpp)
target_link_libraries(augusta_admission_tests PRIVATE augustad_admission GTest::gtest GTest::gtest_main)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_admission_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_replication_tests replication_test.cpp)
target_link_libraries(augusta_replication_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_replication_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_simulation_tests simulation_test.cpp)
target_link_libraries(augusta_simulation_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_tests DISCOVERY_TIMEOUT 30)

# SimulationWorld with a Game policy script the test chooses (ADR-0022).
add_executable(augusta_policy_tests policy_test.cpp)
target_link_libraries(
  augusta_policy_tests
  PRIVATE
    augusta_simulation
    augusta_scripting
    augustad_parameters_loader
    augustad_policy_loader
    augusta_logging
    GTest::gtest
    GTest::gtest_main
)
# A policy script shares no globals with the example scenario's Parameters
# script, and the example's own policy scripts run as the server loads them
# from its golden server pack (ADR-0013).
target_compile_definitions(
  augusta_policy_tests
  PRIVATE
    AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scripts/parameters/rules_of_engagement.lua"
    AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_policy_tests DISCOVERY_TIMEOUT 30)

# What a hook's answer becomes once C++ validates it, apart from any script.
add_executable(augusta_policy_actions_tests policy_actions_test.cpp)
target_link_libraries(augusta_policy_actions_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_simulation, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_policy_actions_tests DISCOVERY_TIMEOUT 30)

# The example scenario's shipped rules, loaded out of a pack built with
# augusta_assets' private encoder and run through SimulationWorld (ADR-0013).
add_executable(augusta_example_rules_tests example_rules_test.cpp)
target_link_libraries(
  augusta_example_rules_tests
  PRIVATE augusta_simulation augustad_policy_loader augusta_logging GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_example_rules_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/assets)
target_compile_definitions(
  augusta_example_rules_tests
  PRIVATE
    AUGUSTA_EXAMPLE_RULES="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scripts/rules/last_man_standing.lua"
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_example_rules_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_networking_tests networking_test.cpp)
target_link_libraries(
  augusta_networking_tests
  PRIVATE augusta_networking GTest::gtest GTest::gtest_main $<$<PLATFORM_ID:Windows>:ws2_32>
)
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
target_link_libraries(
  augusta_send_flags_tests
  PRIVATE
    augusta_networking
    $<IF:$<TARGET_EXISTS:GameNetworkingSockets::GameNetworkingSockets>,GameNetworkingSockets::GameNetworkingSockets,GameNetworkingSockets::GameNetworkingSockets_s>
    GTest::gtest
    GTest::gtest_main
)
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
target_link_libraries(augusta_ballistics_tests PRIVATE augusta_gameplay GTest::gtest GTest::gtest_main)
# NFR-03's golden trajectories (ADR-0013). When the ballistics model changes on
# purpose, one step rewrites them for review and commit:
#   cmake --build --preset <preset> --target augusta_golden_trajectories
target_compile_definitions(
  augusta_ballistics_tests
  PRIVATE AUGUSTA_GOLDEN_TRAJECTORIES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden_trajectories.txt"
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_ballistics_tests DISCOVERY_TIMEOUT 30)
add_custom_target(
  augusta_golden_trajectories
  COMMAND
    augusta_ballistics_tests --gtest_also_run_disabled_tests
    --gtest_filter=BallisticsGoldenTest.DISABLED_RegenerateGoldenFile
  COMMENT "Rewriting tests/fixtures/golden_trajectories.txt"
  VERBATIM
)

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

add_executable(augusta_presentation_tests presentation_test.cpp)
target_link_libraries(augusta_presentation_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_presentation_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_prediction_tests prediction_test.cpp)
# Links augusta_presentation too: a new Match must not show as a correction.
target_link_libraries(
  augusta_prediction_tests
  PRIVATE augusta_prediction augusta_presentation GTest::gtest GTest::gtest_main
)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_prediction_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_map_tests map_test.cpp)
target_link_libraries(augusta_map_tests PRIVATE augusta_physics GTest::gtest GTest::gtest_main)
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
target_link_libraries(
  augusta_session_tests
  PRIVATE
    augusta_harness
    augustad_connection_health
    augustad_host
    augustad_parameters_loader
    augustad_policy_loader
    augusta_logging
    GTest::gtest
    GTest::gtest_main
)
# The server runs the example scenario's Game policy, as it loads it from its
# golden server pack: the Spawn points and the win condition (US-03, US-14).
target_compile_definitions(
  augusta_session_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_session_tests DISCOVERY_TIMEOUT 30)

# augustad's application boundary (ADR-0033): its startup and runtime
# failures, each ending in one classified outcome, on the golden server pack.
add_executable(augusta_server_application_tests server_application_test.cpp)
target_link_libraries(
  augusta_server_application_tests
  PRIVATE
    augustad_application
    augusta_config
    augusta_failure
    augusta_lifecycle
    augusta_logging
    augusta_networking
    augusta_server_config
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(
  augusta_server_application_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_server_application_tests DISCOVERY_TIMEOUT 30)

# A Match capture's file (ADR-0050) through the capture module alone: written,
# read back and refused, with no server; its framing reached through frame.h,
# and the protocol linked only to build malformed files.
add_executable(augusta_capture_format_tests capture_format_test.cpp)
target_link_libraries(
  augusta_capture_format_tests
  PRIVATE augusta_capture augusta_protocol GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_capture_format_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/capture)
gtest_discover_tests(augusta_capture_format_tests)

# Match captures (ADR-0050): a Capturer's files, read back, with no Host.
add_executable(augusta_capturer_tests capturer_test.cpp)
target_link_libraries(augusta_capturer_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_capturer_tests DISCOVERY_TIMEOUT 30)

# A Replay (ADR-0051): a Match capture re-run on a fresh SimulationWorld and
# checked against its markers, with no network.
add_executable(augusta_server_replay_tests server_replay_test.cpp)
target_link_libraries(
  augusta_server_replay_tests
  PRIVATE augustad_host augusta_scripting augusta_simulation GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_server_replay_tests DISCOVERY_TIMEOUT 30)

# Which captures a replay server replays (ADR-0051), from directories of them.
add_executable(augusta_replay_catalog_tests replay_catalog_test.cpp)
target_link_libraries(augusta_replay_catalog_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_replay_catalog_tests DISCOVERY_TIMEOUT 30)

# A replay server (ADR-0051) over loopback, on a Match a live Host captured.
add_executable(augusta_replay_server_tests replay_server_test.cpp)
target_link_libraries(
  augusta_replay_server_tests
  PRIVATE augustad_host augusta_harness augusta_logging augusta_scripting augusta_version GTest::gtest GTest::gtest_main
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_replay_server_tests DISCOVERY_TIMEOUT 30)

# A capture directory kept within a count and a size (ADR-0050, #461).
add_executable(augusta_capture_retention_tests capture_retention_test.cpp)
target_link_libraries(
  augusta_capture_retention_tests
  PRIVATE augustad_host augusta_logging GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_capture_retention_tests DISCOVERY_TIMEOUT 30)

# A Captured player (ADR-0050): its Script read from a capture the capture module
# writes, and its pacing and progress from Server views built by hand.
add_executable(augusta_reenactment_tests reenactment_test.cpp)
target_link_libraries(
  augusta_reenactment_tests
  PRIVATE augusta_harness augustad_commands GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_reenactment_tests DISCOVERY_TIMEOUT 30)

# A Match capture that cannot be written, optional or strict (ADR-0050,
# ADR-0033): its write and flush failures injected (see faults.h), seen from the
# Host and the ServerRuntime running it, with the log lines read off stdout.
add_executable(augusta_capture_runtime_tests capture_runtime_test.cpp)
target_link_libraries(
  augusta_capture_runtime_tests
  PRIVATE
    augustad_host
    augustad_runtime
    augusta_failure
    augusta_gameplay
    augusta_lifecycle
    augusta_logging
    augusta_networking
    GTest::gtest
    GTest::gtest_main
)
# Links networking and PhysX through augustad_host, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_capture_runtime_tests DISCOVERY_TIMEOUT 30)

# What a player's ticks add up to, from Prediction States built by hand.
add_executable(augusta_netcode_stats_tests netcode_stats_test.cpp)
target_link_libraries(
  augusta_netcode_stats_tests
  PRIVATE augusta_harness augusta_math augusta_prediction GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_prediction, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_netcode_stats_tests DISCOVERY_TIMEOUT 30)

# The Command each Intent yields and how it ends, from Server views built by hand.
add_executable(augusta_intent_tests intent_test.cpp)
target_link_libraries(augusta_intent_tests PRIVATE augusta_harness augusta_math GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_harness, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_intent_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_runner_tests runner_test.cpp)
target_link_libraries(augusta_runner_tests PRIVATE augusta_harness augustad_host GTest::gtest GTest::gtest_main)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_runner_tests DISCOVERY_TIMEOUT 30)

# Local transport failures at the runtime seams (ADR-0033): a real Host,
# Session, Runner and ServerRuntime over loopback, the transport made to fail
# by fault injection.
add_executable(augusta_transport_failure_tests transport_failure_test.cpp)
target_link_libraries(
  augusta_transport_failure_tests
  PRIVATE augusta_harness augusta_lifecycle augustad_host augustad_runtime GTest::gtest GTest::gtest_main
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_transport_failure_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_wire_tests wire_test.cpp)
target_link_libraries(augusta_wire_tests PRIVATE augusta_harness augustad_host GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_wire_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_tick_messages_tests tick_messages_test.cpp)
target_link_libraries(augusta_tick_messages_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
# Links networking and PhysX through augustad_host, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_tick_messages_tests DISCOVERY_TIMEOUT 30)

add_executable(augusta_simulation_mapping_tests simulation_mapping_test.cpp)
target_link_libraries(augusta_simulation_mapping_tests PRIVATE augustad_host GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_simulation_mapping_tests DISCOVERY_TIMEOUT 30)

# No header past the protocol's edge names a protocol type (ADR-0038): only
# harness_wire.h and server/wire.h convert. Nor does a presentation header name
# the harness: the client converts (src/client/frame_mapping.h). Nor does the
# protocol itself include or link anything but augusta_math and
# augusta_primitives. A source scan, so it builds nothing.
add_test(
  NAME augusta_protocol_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/protocol_boundary.cmake
)

# Neither physics nor SimulationWorld names the protocol or the client's input
# sampler in its public headers: the grids and the Command they need are shared
# core modules of their own. Nor do the neutral primitives depend on any other
# module, nor does anything else give their counters a type of its own or
# declare their bounds again, even as an alias. A source scan, so it builds
# nothing.
add_test(
  NAME augusta_core_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/core_boundary.cmake
)

# Which platforms augustad configures for, and as what (NFR-04): Linux x86-64 in
# production, Windows x64 and Linux arm64 for development, nothing else
# (cmake/AugustaPlatform.cmake).
# A script, so it builds nothing.
add_test(
  NAME augusta_server_platform
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/server_platform.cmake
)

# The headless server has no client Input dependency (ARCHITECTURE.md Â§5):
# augusta_input stays client-only so the configure-time scope check covers it,
# that check rejects a fixture build graph where a server-only target reaches a
# client-only one, and no server source or server config header names Input.
# It only configures that fixture, with no compiler, so it builds nothing.
add_test(
  NAME augusta_server_boundary
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/server_boundary.cmake
)

# Executables are Glue (ADR-0060): the Glue check refuses a library target
# defined inside an executable's own directory, allows that executable's own
# three and a library outside it, lets a named exception through and counts
# what is left, and refuses an exception naming a library that has moved out.
# It only configures a fixture build graph, with no compiler, so it builds
# nothing.
add_test(
  NAME augusta_glue_boundary
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/glue_boundary.cmake
)

# What the Linux client's tarball is checked for before a release publishes it
# (scripts/stage-client.sh, ADR-0058): the check itself, against a stand-in for
# augustac's build, so it runs on every pull request although augustac's Linux
# build does not.
if(NOT WIN32)
  add_test(
    NAME augusta_stage_client_linux
    COMMAND
      ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/stage_client_linux.cmake
  )
endif()

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
target_link_libraries(
  augusta_cooked_pack_tests
  PRIVATE augusta_assets augusta_cues augustad_policy_loader GTest::gtest GTest::gtest_main
)
target_compile_definitions(
  augusta_cooked_pack_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
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
target_link_libraries(
  augusta_crash_property_tests
  PRIVATE augustad_crash augusta_logging rapidcheck_gtest GTest::gtest GTest::gtest_main
)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23 (see
# augusta_protocol_property_tests).
target_compile_options(augusta_crash_property_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
gtest_discover_tests(augusta_crash_property_tests PROPERTIES LABELS property)

# The renderer's GPU half builds only with the client (AUGUSTA_CLIENT), but
# which region of its per-frame upload buffers a frame uses is a GPU-free
# header, private to the module, so it is tested in every build through a
# private include path.
add_executable(augusta_frame_regions_tests frame_regions_test.cpp)
target_include_directories(augusta_frame_regions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(augusta_frame_regions_tests PRIVATE GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_frame_regions_tests)

# Likewise the geometry the renderer builds on the CPU (vertices.h).
add_executable(augusta_renderer_vertices_tests renderer_vertices_test.cpp)
target_include_directories(augusta_renderer_vertices_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(augusta_renderer_vertices_tests PRIVATE augusta_renderer_vertices GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_renderer_vertices_tests)

# And the debug HUD's text (debug_hud_text.h), which both Renderers draw.
add_executable(augusta_renderer_debug_hud_text_tests renderer_debug_hud_text_test.cpp)
target_include_directories(augusta_renderer_debug_hud_text_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(
  augusta_renderer_debug_hud_text_tests
  PRIVATE augusta_renderer_debug_hud_text GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_renderer_debug_hud_text_tests)

# And which Vulkan device it draws with (device_choice.h).
add_executable(augusta_renderer_device_choice_tests renderer_device_choice_test.cpp)
target_include_directories(augusta_renderer_device_choice_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(
  augusta_renderer_device_choice_tests
  PRIVATE augusta_renderer_device_choice GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_renderer_device_choice_tests)

if(AUGUSTA_CLIENT)
  # The client's private libraries' targets build with the client, on Windows
  # and Linux - though only the smoke test below links the Renderer.
  add_executable(augusta_client_scene_tests client_scene_test.cpp)
  target_link_libraries(augusta_client_scene_tests PRIVATE augustac_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_scene_tests)

  add_executable(augusta_client_character_tests client_character_test.cpp)
  target_link_libraries(augusta_client_character_tests PRIVATE augustac_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_character_tests)

  add_executable(augusta_client_frame_mapping_tests client_frame_mapping_test.cpp)
  target_link_libraries(
    augusta_client_frame_mapping_tests
    PRIVATE augustac_frame_mapping GTest::gtest GTest::gtest_main
  )
  # Links networking and PhysX through the harness, so the same first-launch
  # cost applies as above.
  gtest_discover_tests(augusta_client_frame_mapping_tests DISCOVERY_TIMEOUT 30)

  add_executable(augusta_client_lobby_readiness_tests client_lobby_readiness_test.cpp)
  target_link_libraries(
    augusta_client_lobby_readiness_tests
    PRIVATE augustac_lobby_readiness GTest::gtest GTest::gtest_main
  )
  gtest_discover_tests(augusta_client_lobby_readiness_tests DISCOVERY_TIMEOUT 30)

  add_executable(augusta_client_application_tests client_application_test.cpp)
  target_link_libraries(
    augusta_client_application_tests
    PRIVATE
      augustac_application
      augusta_client_config
      augusta_config
      augusta_failure
      augusta_harness
      GTest::gtest
      GTest::gtest_main
  )
  target_compile_definitions(
    augusta_client_application_tests
    PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
  )
  gtest_discover_tests(augusta_client_application_tests DISCOVERY_TIMEOUT 30)

  add_executable(augusta_client_net_stats_tests client_net_stats_test.cpp)
  target_link_libraries(augusta_client_net_stats_tests PRIVATE augustac_net_stats GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_net_stats_tests DISCOVERY_TIMEOUT 30)

  # Drives the Renderer (ADR-0059) on lavapipe, Mesa's software Vulkan driver,
  # in a hidden window, so it runs on CI's runners, which have no GPU; under the
  # validation layer, whose errors fail it. The layer comes from vcpkg (the
  # manifest's client feature), and ctest points the Vulkan loader at it and at
  # lavapipe, as its only driver, so the test never runs on a GPU. The loader
  # logs its errors and warnings, so a driver it can't load says why.
  # Set by a cmake -E env launcher rather than gtest_discover_tests' PROPERTIES,
  # which splits a list-valued ENVIRONMENT into separate properties and drops
  # all but its first variable. gtest_discover_tests runs the launcher, the
  # target's CROSSCOMPILING_EMULATOR, before discovery and before each test,
  # cross-compiling or not, under CMP0158's OLD behavior, which
  # cmake_minimum_required gives.
  set(vcpkg_installed "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  set(smoke_test_environment "")
  set(no_smoke_test_reason "AUGUSTA_LAVAPIPE is off, so there is no lavapipe to draw on")
  if(WIN32 AND AUGUSTA_LAVAPIPE)
    # On Windows lavapipe comes from vcpkg too (the manifest's lavapipe
    # feature, AUGUSTA_LAVAPIPE). vcpkg's folder, put on PATH, has the DLLs'
    # own dependencies, and a loader for a machine whose GPU driver installed
    # none in System32, as CI's runner's.
    # The loader is given a copy of lavapipe's ICD manifest whose library_path
    # is the driver in vcpkg's bin: Mesa writes that path when it builds the
    # port, in vcpkg's build folder, which a binary-cached package may not
    # share.
    # An elevated process's loader ignores VK_DRIVER_FILES and
    # VK_ADD_LAYER_PATH, so CI's runner, whose processes are, registers both
    # instead (ci.yml).
    set(lavapipe_icd "${vcpkg_installed}/share/vulkan/icd.d/lvp_icd.json")
    if(NOT EXISTS "${lavapipe_icd}" OR NOT EXISTS "${vcpkg_installed}/bin/vulkan-1.dll")
      message(FATAL_ERROR "AUGUSTA_LAVAPIPE is on, but vcpkg installed no ${lavapipe_icd} or vulkan-1.dll")
    endif()
    file(READ "${lavapipe_icd}" icd)
    string(JSON lavapipe_library GET "${icd}" ICD library_path)
    file(TO_CMAKE_PATH "${lavapipe_library}" lavapipe_library)
    cmake_path(GET lavapipe_library FILENAME lavapipe_library)
    set(lavapipe_library "${vcpkg_installed}/bin/${lavapipe_library}")
    if(NOT EXISTS "${lavapipe_library}")
      message(FATAL_ERROR "lavapipe's ICD manifest names ${lavapipe_library}, which vcpkg didn't install")
    endif()
    string(JSON icd SET "${icd}" ICD library_path "\"${lavapipe_library}\"")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lvp_icd.json" "${icd}")
    set(
      smoke_test_environment
      "VK_DRIVER_FILES=${CMAKE_CURRENT_BINARY_DIR}/lvp_icd.json;VK_ADD_LAYER_PATH=${vcpkg_installed}/bin;VK_LOADER_DEBUG=error,warn;--modify;PATH=path_list_prepend:${vcpkg_installed}/bin"
    )
  elseif(NOT WIN32)
    # On Linux lavapipe is the system's (Mesa's Vulkan drivers, which
    # scripts/bootstrap/linux.sh installs), through the system's loader, as the
    # client itself runs, and its window needs a display: CI's runner gives it
    # a virtual X server (xvfb-run, nightly-jobs.yml). The layer's manifest
    # names its library without a folder, which the loader then looks for on
    # the library path: vcpkg's lib, put on it.
    find_file(
      AUGUSTA_LAVAPIPE_ICD
      NAMES lvp_icd.json lvp_icd.x86_64.json
      PATHS /usr/share/vulkan/icd.d
      NO_DEFAULT_PATH
      DOC "The system's lavapipe ICD manifest, which the renderer smoke test draws on"
    )
    set(no_smoke_test_reason "no lavapipe in /usr/share/vulkan/icd.d (Mesa's Vulkan drivers)")
    if(AUGUSTA_LAVAPIPE_ICD)
      set(
        smoke_test_environment
        "VK_DRIVER_FILES=${AUGUSTA_LAVAPIPE_ICD};VK_ADD_LAYER_PATH=${vcpkg_installed}/share/vulkan/explicit_layer.d;VK_LOADER_DEBUG=error,warn;--modify;LD_LIBRARY_PATH=path_list_prepend:${vcpkg_installed}/lib"
      )
    endif()
  endif()
  if(smoke_test_environment)
    add_executable(augusta_client_renderer_smoke_tests client_renderer_smoke_test.cpp)
    target_link_libraries(augusta_client_renderer_smoke_tests PRIVATE augusta_renderer GTest::gtest GTest::gtest_main)
    augusta_stage_aftermath(augusta_client_renderer_smoke_tests)
    set_target_properties(
      augusta_client_renderer_smoke_tests
      PROPERTIES CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;${smoke_test_environment};--"
    )
    gtest_discover_tests(augusta_client_renderer_smoke_tests)
  else()
    message(STATUS "Renderer smoke test not built: ${no_smoke_test_reason}")
  endif()

  # The Renderer's GLFW key mapping (glfw_keys.h), private to the module.
  # The test names keys by GLFW's own constants.
  find_package(glfw3 CONFIG REQUIRED)
  add_executable(augusta_client_renderer_glfw_keys_tests client_renderer_glfw_keys_test.cpp)
  target_include_directories(augusta_client_renderer_glfw_keys_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
  target_link_libraries(
    augusta_client_renderer_glfw_keys_tests
    PRIVATE augusta_renderer_glfw_keys glfw GTest::gtest GTest::gtest_main
  )
  gtest_discover_tests(augusta_client_renderer_glfw_keys_tests)
endif()

add_subdirectory(fuzz)

# Every executable here and in fuzz/, and in each tool's tests/ (kept next to
# the tool) when AUGUSTA_TOOLS builds the tools:
# what ctest, the benchmarks and the fuzz jobs run. Each directory is
# EXCLUDE_FROM_ALL, so this is how they get built. Custom targets (the seed
# regenerators) stay out: they rewrite committed files.
add_custom_target(augusta_tests)
set(test_directories ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
if(AUGUSTA_TOOLS)
  list(APPEND test_directories ${CMAKE_SOURCE_DIR}/tools/agent/tests)
endif()
foreach(directory IN LISTS test_directories)
  get_property(directory_targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS directory_targets)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
      add_dependencies(augusta_tests ${target})
    endif()
  endforeach()
endforeach()

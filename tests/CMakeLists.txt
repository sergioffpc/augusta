include(GoogleTest)

# No vcpkg z-applocal step on the test executables, here and in fuzz/:
# augusta_test_dlls (at the end) puts their DLLs beside them.
set(VCPKG_APPLOCAL_DEPS OFF)

# Discovery runs each test binary once, at build time, within 5 s by default,
# which CMake takes from no variable. A binary's first launch can take longer,
# while dozens link side by side: one that links PhysX (the targets below that
# say so), and on macOS every new executable, which the system scans before it
# first runs. Each call gives 30 s.

add_executable(augusta_version_tests version_test.cpp)
target_link_libraries(augusta_version_tests PRIVATE augusta_version GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_version_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS version)

add_executable(augusta_logging_tests logging_test.cpp)
target_link_libraries(augusta_logging_tests PRIVATE augusta_logging GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_logging_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS logging)

# proc_files.h is private to the module, hence its directory.
add_executable(augusta_hardware_tests hardware_test.cpp)
target_include_directories(augusta_hardware_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/hardware)
target_link_libraries(augusta_hardware_tests PRIVATE augusta_hardware GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_hardware_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS hardware)

add_executable(augusta_version_benchmarks version_benchmark.cpp)
target_link_libraries(augusta_version_benchmarks PRIVATE augusta_version benchmark::benchmark benchmark::benchmark_main)

# The hot paths the nightly times and keeps the history of (ADR-0013): one
# binary, augusta_benchmarks, so one run gives one result file for the whole
# set. Compiled once, into objects, which the allocator benchmarks below link
# too.
add_library(
  augusta_hot_path_benchmarks
  OBJECT
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
# its golden server pack; replication is host::Host's own
# (augusta_host), and a render frame plays the golden client pack's cue sounds.
# Its players' Clips are authored with ozz-animation's offline libraries, and
# checked as they are built with GoogleTest's assertions (clips_fixture.h).
target_link_libraries(
  augusta_hot_path_benchmarks
  PUBLIC
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    augusta_assets
    augusta_audio
    augusta_cues
    augusta_gameplay
    augusta_limits
    augusta_presentation
    augusta_protocol
    augusta_simulation
    augusta_version
    augusta_host_replay
    augusta_policy_loader
    benchmark::benchmark
)
target_include_directories(augusta_hot_path_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host)
target_compile_definitions(
  augusta_hot_path_benchmarks
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
add_executable(augusta_benchmarks)
target_link_libraries(augusta_benchmarks PRIVATE augusta_hot_path_benchmarks benchmark::benchmark_main)

# The allocator benchmarks (#1351): the hot paths above, and a churn of blocks
# freed on another thread than the one that allocated them, under mimalloc,
# the allocator chosen to replace malloc (#1397), against the platform's own:
# whether mimalloc still beats it, asked again after a mimalloc bump or on a
# new OS. Built only when the configure asks for them
# (AUGUSTA_ALLOCATOR_BENCHMARKS, the root CMakeLists.txt), and run only by
# their label, allocator, which every test preset leaves out:
# `make test TAGS="allocator"`. One at a time (RUN_SERIAL), so ctest's
# --parallel adds no contention to what they measure. Each writes its Google
# Benchmark JSON into allocator-results/, and first fails if its candidate does
# not serve the process's allocations (allocator_candidate.h).
if(AUGUSTA_ALLOCATOR_BENCHMARKS)
  add_library(augusta_allocator_benchmark_common OBJECT allocator_benchmark_main.cpp allocator_churn_benchmark.cpp)
  target_link_libraries(augusta_allocator_benchmark_common PUBLIC benchmark::benchmark)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/allocator-results)

  # One candidate's executable, augusta_allocator_benchmarks_<candidate>, from
  # SOURCES (its allocator_candidate.h) linked with LIBRARIES, and the test
  # that runs it, five repetitions as the nightly runs augusta_benchmarks, but
  # with every repetition's row in the file beside the aggregates, not the
  # aggregates alone: only those rows carry the resident set. LIBRARIES go
  # first on the link line, so that on Windows their DLLs come first in the
  # import table and load before the CRT's, as mimalloc's redirect needs. Both
  # object libraries are linked directly: an object library's objects reach
  # only the targets that link it themselves, not through another.
  function(augusta_allocator_benchmark candidate)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    set(target augusta_allocator_benchmarks_${candidate})
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(
      ${target}
      PRIVATE ${arg_LIBRARIES} augusta_allocator_benchmark_common augusta_hot_path_benchmarks
    )
    add_test(
      NAME allocator_${candidate}
      COMMAND
        ${target} --benchmark_repetitions=5 --benchmark_display_aggregates_only=true
        --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/allocator-results/${candidate}.json --benchmark_out_format=json
    )
    set_tests_properties(
      allocator_${candidate}
      PROPERTIES LABELS allocator RUN_SERIAL TRUE TIMEOUT 3600
    )
  endfunction()

  augusta_allocator_benchmark(platform SOURCES allocator_platform.cpp)

  find_package(mimalloc CONFIG REQUIRED)
  # Static where the triplet links statically (Linux, macOS), where its malloc
  # replaces the C library's at link time; the DLL on Windows, to which its
  # mimalloc-redirect.dll redirects the CRT's malloc at load time.
  augusta_allocator_benchmark(
    mimalloc
    SOURCES allocator_mimalloc.cpp
    LIBRARIES $<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc>
  )
endif()

# Links augusta_tick alone, so the build fails if its header ever needs another
# module's include path.
add_executable(augusta_tick_tests tick_test.cpp)
target_link_libraries(augusta_tick_tests PRIVATE augusta_tick GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_tick_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS tick)

add_executable(augusta_supervisor_tests supervisor_test.cpp)
target_link_libraries(
  augusta_supervisor_tests
  PRIVATE augusta_failure augusta_supervisor GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_supervisor_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS supervisor)

add_executable(augusta_failure_tests failure_test.cpp)
target_link_libraries(augusta_failure_tests PRIVATE augusta_failure GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_failure_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS failure)

add_executable(augusta_application_tests application_test.cpp)
target_link_libraries(
  augusta_application_tests
  PRIVATE augusta_application augusta_failure augusta_logging GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_application_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS application)

add_executable(augusta_config_tests config_test.cpp)
target_link_libraries(augusta_config_tests PRIVATE augusta_config GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_config_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS config)

# The example configs are what a new install is copied from, so a test keeps each loading.
add_executable(augusta_client_config_tests client_config_test.cpp)
target_link_libraries(augusta_client_config_tests PRIVATE augusta_client_config GTest::gtest GTest::gtest_main)
target_compile_definitions(
  augusta_client_config_tests
  PRIVATE AUGUSTA_EXAMPLE_CLIENT_CONFIG="${CMAKE_SOURCE_DIR}/config/augustac.example.yaml"
)
# Links PhysX through augusta_input, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_client_config_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS config)

add_executable(augusta_server_config_tests server_config_test.cpp)
target_link_libraries(augusta_server_config_tests PRIVATE augusta_server_config GTest::gtest GTest::gtest_main)
target_compile_definitions(
  augusta_server_config_tests
  PRIVATE AUGUSTA_EXAMPLE_SERVER_CONFIG="${CMAKE_SOURCE_DIR}/config/augustad.example.yaml"
)
gtest_discover_tests(augusta_server_config_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS config)

add_executable(augusta_parameters_tests parameters_test.cpp)
target_link_libraries(augusta_parameters_tests PRIVATE augusta_gameplay augusta_limits GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_parameters_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gameplay)

add_executable(augusta_parameters_loader_tests parameters_loader_test.cpp)
target_link_libraries(
  augusta_parameters_loader_tests
  PRIVATE augusta_scenario_parameters_loader GTest::gtest GTest::gtest_main
)
# The example scenario's script is what a new scenario is copied from, so a
# test keeps it loading.
target_compile_definitions(
  augusta_parameters_loader_tests
  PRIVATE
    AUGUSTA_EXAMPLE_PARAMETERS="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scripts/parameters/rules_of_engagement.lua"
)
target_include_directories(augusta_parameters_loader_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/scenario)
gtest_discover_tests(augusta_parameters_loader_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS scenario)

add_executable(augusta_protocol_tests protocol_test.cpp)
target_link_libraries(
  augusta_protocol_tests
  PRIVATE augusta_limits augusta_protocol augusta_tick augusta_version GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_protocol_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS protocol)

# A Match capture's records, in the protocol's encoding (ADR-0050).
add_executable(augusta_protocol_capture_tests protocol_capture_test.cpp)
target_link_libraries(augusta_protocol_capture_tests PRIVATE augusta_protocol GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_protocol_capture_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS protocol)

add_executable(augusta_protocol_property_tests protocol_property_test.cpp)
target_link_libraries(
  augusta_protocol_property_tests
  PRIVATE augusta_limits augusta_protocol augusta_tick rapidcheck_gtest GTest::gtest GTest::gtest_main
)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23. Clang
# reports it where our code instantiates the template, so its system include
# path does not silence it; nothing of ours is deprecated in this target.
target_compile_options(augusta_protocol_property_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
# The property label is how the nightly selects the property tests to run at
# a high case count (ADR-0013).
gtest_discover_tests(augusta_protocol_property_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "protocol;property")

add_executable(augusta_simulation_property_tests simulation_property_test.cpp)
target_link_libraries(
  augusta_simulation_property_tests
  PRIVATE
    augusta_simulation
    augusta_prediction
    augusta_animation
    augusta_logging
    ozz::animation_offline
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
gtest_discover_tests(augusta_simulation_property_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "simulation;property")

add_executable(augusta_match_tests match_test.cpp)
target_link_libraries(augusta_match_tests PRIVATE augusta_host_match GTest::gtest GTest::gtest_main)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_match_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_command_queue_tests command_queue_test.cpp)
target_link_libraries(augusta_command_queue_tests PRIVATE augusta_host_commands GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_command_queue_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_misbehaviour_tests misbehaviour_test.cpp)
target_link_libraries(augusta_misbehaviour_tests PRIVATE augusta_host_misbehaviour GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_misbehaviour_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_heartbeat_tests heartbeat_test.cpp)
target_link_libraries(augusta_heartbeat_tests PRIVATE augusta_host_heartbeat GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_heartbeat_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_metrics_liveness_tests metrics_liveness_test.cpp)
target_link_libraries(
  augusta_metrics_liveness_tests
  PRIVATE augusta_server_metrics_liveness GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_metrics_liveness_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
gtest_discover_tests(augusta_metrics_liveness_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS server)

# Over real HTTP on the loopback, on a port the test finds free, with the
# acceptor's failures injected (see faults.h) and the log line they end in read
# off stdout.
find_package(boost_beast CONFIG REQUIRED)
add_executable(augusta_metrics_tests metrics_test.cpp)
target_link_libraries(
  augusta_metrics_tests
  PRIVATE
    augusta_server_metrics
    augusta_server_metrics_liveness
    augusta_failure
    augusta_logging
    Boost::beast
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(augusta_metrics_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
gtest_discover_tests(augusta_metrics_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS server)

# What the Host counts, collected as the endpoint collects it, without HTTP.
add_executable(augusta_host_metrics_tests host_metrics_test.cpp)
target_link_libraries(augusta_host_metrics_tests PRIVATE augusta_host GTest::gtest GTest::gtest_main)
# Links networking and PhysX through augusta_host, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_host_metrics_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# Reads what it records back as the metrics endpoint collects it.
add_executable(augusta_metrics_connection_health_tests metrics_connection_health_test.cpp)
target_link_libraries(
  augusta_metrics_connection_health_tests
  PRIVATE augusta_server_metrics_connection_health GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_metrics_connection_health_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/server)
# Links networking (for ConnectionStats), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_metrics_connection_health_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS server)

add_executable(augusta_admission_tests admission_test.cpp)
target_link_libraries(augusta_admission_tests PRIVATE augusta_host_admission GTest::gtest GTest::gtest_main)
target_include_directories(augusta_admission_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host)
# Links networking (for PeerId), so the same first-launch cost applies as below.
gtest_discover_tests(augusta_admission_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_replication_tests replication_test.cpp)
target_link_libraries(augusta_replication_tests PRIVATE augusta_simulation GTest::gtest GTest::gtest_main)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_replication_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS simulation)

add_executable(augusta_simulation_tests simulation_test.cpp hitbox_history_test.cpp)
# Links augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
target_link_libraries(
  augusta_simulation_tests
  PRIVATE augusta_simulation augusta_animation ozz::animation_offline GTest::gtest GTest::gtest_main
)
# The Hitbox history is private to the simulation module: its test reaches its
# header there.
target_include_directories(augusta_simulation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/simulation)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_simulation_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS simulation)

# The Shooter's delay's cap held to a real client's budget: simulation's cap
# against presentation's Interpolation delay (ADR-0044).
add_executable(augusta_shooters_delay_budget_tests shooters_delay_budget_test.cpp)
target_link_libraries(
  augusta_shooters_delay_budget_tests
  PRIVATE augusta_simulation augusta_presentation GTest::gtest GTest::gtest_main
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_shooters_delay_budget_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS simulation)

# Client and server agree on the pose (ADR-0061): simulation's Authoritative
# State over the protocol to presentation's interpolation, each posed by
# animation. Links ozz-animation's offline libraries for the Clips it authors
# (clips_fixture.h).
add_executable(augusta_pose_agreement_tests pose_agreement_test.cpp)
target_link_libraries(
  augusta_pose_agreement_tests
  PRIVATE
    augusta_simulation
    augusta_presentation
    augusta_link_messages
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    GTest::gtest_main
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_pose_agreement_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS simulation)

# SimulationWorld with a Game policy script the test chooses (ADR-0022). Links
# augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
add_executable(augusta_policy_tests policy_test.cpp)
target_link_libraries(
  augusta_policy_tests
  PRIVATE
    augusta_simulation
    augusta_animation
    ozz::animation_offline
    augusta_policy
    augusta_scenario_parameters_loader
    augusta_policy_loader
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
target_include_directories(augusta_policy_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/scenario)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_policy_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS policy)

# What a hook's answer becomes once C++ validates it, apart from any script:
# the policy module's private reading of answers (answers.h).
add_executable(augusta_policy_actions_tests policy_actions_test.cpp)
target_include_directories(augusta_policy_actions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/policy)
target_link_libraries(
  augusta_policy_actions_tests
  PRIVATE augusta_policy augusta_scripting GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_policy_actions_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS policy)

# The example scenario's shipped rules, loaded out of a pack built with
# augusta_assets' pack encoder and run through SimulationWorld (ADR-0013).
# Links augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
add_executable(augusta_example_rules_tests example_rules_test.cpp)
target_link_libraries(
  augusta_example_rules_tests
  PRIVATE
    augusta_simulation
    augusta_policy_loader
    augusta_logging
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(
  augusta_example_rules_tests
  PRIVATE
    AUGUSTA_EXAMPLE_RULES="${CMAKE_SOURCE_DIR}/tools/composer/examples/authoring/scripts/rules/last_man_standing.lua"
)
# Links PhysX, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_example_rules_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS policy)

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
gtest_discover_tests(augusta_networking_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS networking)

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
gtest_discover_tests(augusta_send_flags_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS networking)

# What a transport event does to the peers is decided in a header private to
# the networking module, tested apart from a live connection the same way.
add_executable(augusta_transport_events_tests transport_events_test.cpp)
target_include_directories(augusta_transport_events_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/networking)
target_link_libraries(augusta_transport_events_tests PRIVATE augusta_networking GTest::gtest GTest::gtest_main)
# Links GameNetworkingSockets, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_transport_events_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS networking)

add_executable(augusta_physics_tests physics_test.cpp physx_allocator_test.cpp)
# The allocator PhysX is given is private to the physics module, tested through
# its header the same way. That header includes PhysX's own, whose libraries
# reach the test through augusta_physics already, so only their headers are
# added here: linking the SDK again would bypass the module's link group on Linux.
target_include_directories(augusta_physics_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/physics)
target_include_directories(
  augusta_physics_tests
  SYSTEM
  PRIVATE $<TARGET_PROPERTY:unofficial::omniverse-physx-sdk::sdk,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(augusta_physics_tests PRIVATE augusta_physics GTest::gtest GTest::gtest_main)
# DISCOVERY_TIMEOUT above the 5s default, same reason as the networking tests
# above: the PhysX DLLs make a fresh build's first launch slow under Windows Defender.
gtest_discover_tests(augusta_physics_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS physics)

add_executable(augusta_body_part_tests body_part_test.cpp)
target_link_libraries(augusta_body_part_tests PRIVATE augusta_gameplay GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_body_part_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gameplay)

add_executable(augusta_hitbox_tests hitbox_test.cpp)
target_link_libraries(augusta_hitbox_tests PRIVATE augusta_gameplay GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_hitbox_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gameplay)

# A Character's Clips loaded from packs the test writes, of archives it
# authors with ozz-animation's offline libraries, as the cooker does.
add_executable(augusta_clips_tests clips_test.cpp)
target_link_libraries(
  augusta_clips_tests
  PRIVATE augusta_animation augusta_assets ozz::animation_offline GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_clips_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS animation)

# A stance's blend space and the stride length a Stride advances by, on Clips
# the test authors in memory (clips_fixture.h).
add_executable(augusta_locomotion_tests locomotion_test.cpp)
target_link_libraries(
  augusta_locomotion_tests
  PRIVATE augusta_animation ozz::animation_offline GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_locomotion_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS animation)

# A Hitbox pose evaluated from Clips the test authors in memory
# (clips_fixture.h), whose Joints move.
add_executable(augusta_pose_tests pose_test.cpp)
target_link_libraries(
  augusta_pose_tests
  PRIVATE augusta_animation ozz::animation_offline GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_pose_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS animation)

# What the server loads at startup, on the golden server pack and on copies of
# it the test re-signs without an entry.
add_executable(augusta_server_content_tests server_content_test.cpp)
target_link_libraries(augusta_server_content_tests PRIVATE augusta_scenario GTest::gtest GTest::gtest_main)
target_compile_definitions(
  augusta_server_content_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links PhysX through augusta_gameplay, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_server_content_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS scenario)

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
gtest_discover_tests(augusta_ballistics_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gameplay)
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
target_include_directories(augusta_reconciliation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/prediction)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_reconciliation_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS prediction)

add_executable(augusta_correction_tests correction_test.cpp)
target_link_libraries(augusta_correction_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
target_include_directories(augusta_correction_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/presentation)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_correction_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_input_tests input_test.cpp)
target_link_libraries(augusta_input_tests PRIVATE augusta_input GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_input_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS input)

add_executable(augusta_interpolation_tests interpolation_test.cpp)
target_link_libraries(augusta_interpolation_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_interpolation_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_local_view_tests local_view_test.cpp)
target_link_libraries(augusta_local_view_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_local_view_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_tracers_tests tracers_test.cpp)
target_link_libraries(augusta_tracers_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_tracers_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_effects_tests effects_test.cpp)
target_link_libraries(augusta_effects_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_effects_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_audio_cues_tests audio_cues_test.cpp)
target_link_libraries(augusta_audio_cues_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
target_include_directories(augusta_audio_cues_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/presentation)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_audio_cues_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_presentation_tests presentation_test.cpp)
# Links augusta_animation and ozz-animation's offline libraries for the Clips of
# the local player's Character it authors (clips_fixture.h).
target_link_libraries(
  augusta_presentation_tests
  PRIVATE augusta_presentation augusta_animation ozz::animation_offline GTest::gtest GTest::gtest_main
)
# Links augusta_prediction publicly (so PhysX), same first-launch cost as above.
gtest_discover_tests(augusta_presentation_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)

add_executable(augusta_prediction_tests prediction_test.cpp)
# Links augusta_presentation too: a new Match must not show as a correction. And
# augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
target_link_libraries(
  augusta_prediction_tests
  PRIVATE
    augusta_prediction
    augusta_presentation
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(augusta_prediction_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/presentation)
# Links augusta_physics publicly, so the same PhysX DLL launch cost applies.
gtest_discover_tests(augusta_prediction_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS prediction)

add_executable(augusta_map_tests map_test.cpp)
target_link_libraries(augusta_map_tests PRIVATE augusta_physics GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as below.
gtest_discover_tests(augusta_map_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS physics)

add_executable(augusta_policy_loader_tests policy_loader_test.cpp)
target_link_libraries(augusta_policy_loader_tests PRIVATE augusta_policy_loader GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_policy_loader_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS policy)

# Links augusta_animation and ozz-animation's offline libraries for the Clips its
# prediction is built with (clips_fixture.h).
add_executable(augusta_session_tests session_test.cpp)
target_link_libraries(
  augusta_session_tests
  PRIVATE
    augusta_harness
    augusta_animation
    ozz::animation_offline
    augusta_server_metrics_connection_health
    augusta_host
    augusta_link
    augusta_link_raw
    augusta_link_messages
    augusta_protocol
    augusta_scenario_parameters_loader
    augusta_policy_loader
    augusta_logging
    GTest::gtest
    GTest::gtest_main
)
# metrics_connection_health.h is private to the server Runtime, reached through
# its directory.
target_include_directories(
  augusta_session_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/modules/host
    ${CMAKE_SOURCE_DIR}/src/modules/link
    ${CMAKE_SOURCE_DIR}/src/modules/scenario
)
# The server runs the example scenario's Game policy, as it loads it from its
# golden server pack: the Spawn points and the win condition (US-03, US-14).
target_compile_definitions(
  augusta_session_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_session_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# augustad's application boundary (ADR-0033): its startup and runtime
# failures, each ending in one classified outcome, on the golden server pack.
add_executable(augusta_server_application_tests server_application_test.cpp)
target_link_libraries(
  augusta_server_application_tests
  PRIVATE
    augusta_server_application
    augusta_application
    augusta_assets
    augusta_config
    augusta_failure
    augusta_logging
    augusta_link
    augusta_server_config
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(
  augusta_server_application_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_server_application_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS server)

# augustad as built runs on mimalloc (ADR-0078): it checks the allocator before
# it reads its command line, so its --version exits 0 only when mimalloc serves
# it, a broken link or redirect failing it. Not under a sanitizer, whose own
# allocator serves.
if(NOT AUGUSTA_SANITIZER_ALLOCATOR)
  add_test(NAME augustad_version COMMAND augustad --version)
  set_tests_properties(augustad_version PROPERTIES LABELS server)
endif()

# A Match capture's file (ADR-0050) through the capture module alone: written,
# read back and refused, with no server; its framing reached through frame.h,
# and the protocol linked only to build malformed files.
add_executable(augusta_capture_format_tests capture_format_test.cpp)
target_link_libraries(
  augusta_capture_format_tests
  PRIVATE augusta_capture augusta_protocol GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_capture_format_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/capture)
gtest_discover_tests(augusta_capture_format_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS capture)

# Match captures (ADR-0050): a Capturer's files, read back, with no Host.
add_executable(augusta_capturer_tests capturer_test.cpp)
target_link_libraries(augusta_capturer_tests PRIVATE augusta_host GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_capturer_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# A Replay (ADR-0051): a Match capture re-run on a fresh SimulationWorld and
# checked against its markers, with no network.
add_executable(augusta_server_replay_tests server_replay_test.cpp)
# Links augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
target_link_libraries(
  augusta_server_replay_tests
  PRIVATE
    augusta_host
    augusta_policy
    augusta_simulation
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(augusta_server_replay_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# Which captures a replay server replays (ADR-0051), from directories of them.
add_executable(augusta_replay_catalog_tests replay_catalog_test.cpp)
target_link_libraries(augusta_replay_catalog_tests PRIVATE augusta_host GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_replay_catalog_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# A replay server (ADR-0051) over loopback, on a Match a live Host captured.
# Links augusta_animation and ozz-animation's offline libraries for the Clips
# its viewers' prediction is built with (clips_fixture.h).
add_executable(augusta_replay_server_tests replay_server_test.cpp)
target_link_libraries(
  augusta_replay_server_tests
  PRIVATE
    augusta_host_replay
    augusta_animation
    ozz::animation_offline
    augusta_harness
    augusta_link
    augusta_link_raw
    augusta_link_messages
    augusta_logging
    augusta_protocol
    augusta_policy
    augusta_version
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(augusta_replay_server_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/link)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_replay_server_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# A capture directory kept within a count and a size (ADR-0050, #461).
add_executable(augusta_capture_retention_tests capture_retention_test.cpp)
target_link_libraries(
  augusta_capture_retention_tests
  PRIVATE augusta_host augusta_logging GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_capture_retention_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# A Captured player (ADR-0050): its Script read from a capture the capture module
# writes, and its pacing and progress from Server views built by hand.
add_executable(augusta_reenactment_tests reenactment_test.cpp)
target_link_libraries(
  augusta_reenactment_tests
  PRIVATE augusta_harness augusta_host_commands GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_reenactment_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS harness)

# A Match capture that cannot be written, optional or strict (ADR-0050,
# ADR-0033): its write and flush failures injected (see faults.h), seen from the
# Host and the ServerRuntime running it, with the log lines read off stdout.
add_executable(augusta_capture_runtime_tests capture_runtime_test.cpp)
target_link_libraries(
  augusta_capture_runtime_tests
  PRIVATE
    augusta_host
    augusta_server
    augusta_failure
    augusta_gameplay
    augusta_logging
    augusta_link
    augusta_supervisor
    GTest::gtest
    GTest::gtest_main
)
# Links networking and PhysX through augusta_host, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_capture_runtime_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# What a player's ticks add up to, from Prediction States built by hand.
add_executable(augusta_netcode_stats_tests netcode_stats_test.cpp)
target_link_libraries(
  augusta_netcode_stats_tests
  PRIVATE augusta_harness augusta_math augusta_prediction GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_prediction, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_netcode_stats_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS harness)

# How often and how far Reconciliation moved the body, from Prediction States built by hand.
add_executable(augusta_correction_tally_tests correction_tally_test.cpp)
target_link_libraries(
  augusta_correction_tally_tests
  PRIVATE augusta_math augusta_prediction GTest::gtest GTest::gtest_main
)
# Links PhysX through augusta_prediction, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_correction_tally_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS prediction)

# The Command each Intent yields and how it ends, from Server views built by hand.
add_executable(augusta_intent_tests intent_test.cpp)
target_link_libraries(augusta_intent_tests PRIVATE augusta_harness augusta_math GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_harness, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_intent_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS harness)

# Links augusta_animation and ozz-animation's offline libraries for the Clips its
# prediction is built with (clips_fixture.h).
add_executable(augusta_runner_tests runner_test.cpp)
target_link_libraries(
  augusta_runner_tests
  PRIVATE
    augusta_harness
    augusta_host
    augusta_link
    augusta_protocol
    augusta_animation
    ozz::animation_offline
    GTest::gtest
    GTest::gtest_main
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_runner_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS harness)

# Local transport failures at the runtime seams (ADR-0033): a real Host,
# Session, Runner and ServerRuntime over loopback, the transport made to fail
# by fault injection. Links augusta_animation and ozz-animation's offline
# libraries for the Clips its prediction is built with (clips_fixture.h).
add_executable(augusta_transport_failure_tests transport_failure_test.cpp)
target_link_libraries(
  augusta_transport_failure_tests
  PRIVATE
    augusta_harness
    augusta_animation
    ozz::animation_offline
    augusta_host
    augusta_link
    augusta_link_raw
    augusta_supervisor
    augusta_server
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(
  augusta_transport_failure_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host ${CMAKE_SOURCE_DIR}/src/modules/link
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_transport_failure_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# The Link's conversions (ADR-0038), with no transport: what one peer sends
# reaches the other's engine types unchanged, and a tick's Commands are split
# at the protocol's bound.
add_executable(augusta_link_messages_tests link_messages_test.cpp)
target_link_libraries(augusta_link_messages_tests PRIVATE augusta_link_messages GTest::gtest GTest::gtest_main)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_link_messages_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS link)

# What the Link decides about the transport beneath it is in headers private to
# the link module, tested apart from a live connection through a private
# include path, as networking's send flags are. What a Client counts it carries
# is tested over loopback, against a raw server that sends any bytes.
add_executable(augusta_link_transport_tests link_transport_test.cpp)
target_include_directories(augusta_link_transport_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/link)
target_link_libraries(
  augusta_link_transport_tests
  PRIVATE
    augusta_link
    augusta_link_messages
    augusta_link_raw
    augusta_networking
    augusta_protocol
    GTest::gtest
    GTest::gtest_main
)
# Links networking and PhysX, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_link_transport_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS link)

add_executable(augusta_tick_messages_tests tick_messages_test.cpp)
target_link_libraries(augusta_tick_messages_tests PRIVATE augusta_host augusta_protocol GTest::gtest GTest::gtest_main)
target_include_directories(augusta_tick_messages_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host)
# Links networking and PhysX through augusta_host, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_tick_messages_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_simulation_mapping_tests simulation_mapping_test.cpp)
# Links augusta_animation and ozz-animation's offline libraries for the Clips it
# authors (clips_fixture.h).
target_link_libraries(
  augusta_simulation_mapping_tests
  PRIVATE augusta_host augusta_animation ozz::animation_offline GTest::gtest GTest::gtest_main
)
target_include_directories(augusta_simulation_mapping_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_simulation_mapping_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

# The example soldier holding the example rifle, as a server loads and poses
# them from the golden server pack (ADR-0064, ADR-0066).
add_executable(augusta_example_soldier_tests example_soldier_test.cpp)
target_link_libraries(augusta_example_soldier_tests PRIVATE augusta_host GTest::gtest GTest::gtest_main)
target_include_directories(augusta_example_soldier_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/host)
target_compile_definitions(
  augusta_example_soldier_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
# Links PhysX through augusta_physics, so the same first-launch cost applies as above.
gtest_discover_tests(augusta_example_soldier_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS host)

add_executable(augusta_assets_tests assets_test.cpp)
target_link_libraries(augusta_assets_tests PRIVATE augusta_assets GTest::gtest GTest::gtest_main)
# WritePack/Encode*/Pack::Load round-trip coverage, through augusta_assets'
# public pack encoder (augusta/pack_encoder.h).
gtest_discover_tests(augusta_assets_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS assets)

# The client's cue sounds; builds real packs with augusta_assets' pack
# encoder, as augusta_assets_tests does.
add_executable(augusta_cues_tests cues_test.cpp)
target_link_libraries(augusta_cues_tests PRIVATE augusta_assets augusta_cues GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_cues_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS audio)

# Loads the golden packs the Python cooker wrote from its example scenario
# (ADR-0013); the cooker's own tests keep it writing them byte for byte.
add_executable(augusta_cooked_pack_tests cooked_pack_test.cpp)
# Also loads the example's Game policy scripts as the server does (ADR-0022),
# and every cue's sound as the client does.
target_link_libraries(
  augusta_cooked_pack_tests
  PRIVATE augusta_assets augusta_cues augusta_policy_loader GTest::gtest GTest::gtest_main
)
target_compile_definitions(
  augusta_cooked_pack_tests
  PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
)
gtest_discover_tests(augusta_cooked_pack_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS assets)

# The crash lines' format, against the log sink's, and the handler itself in
# death tests; on Windows they read the minidump a crash writes through dbghelp.
add_executable(augusta_crash_tests crash_test.cpp)
target_link_libraries(
  augusta_crash_tests
  PRIVATE augusta_crash augusta_logging GTest::gtest GTest::gtest_main $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_compile_definitions(augusta_crash_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
gtest_discover_tests(augusta_crash_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS crash)

# The crash lines' timestamp against the log sink's over every second to the
# year 9999.
add_executable(augusta_crash_property_tests crash_property_test.cpp)
target_link_libraries(
  augusta_crash_property_tests
  PRIVATE augusta_crash augusta_logging rapidcheck_gtest GTest::gtest GTest::gtest_main
)
# RapidCheck's Maybe uses std::aligned_storage, deprecated since C++23 (see
# augusta_protocol_property_tests).
target_compile_options(augusta_crash_property_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)
gtest_discover_tests(augusta_crash_property_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "crash;property")

# The renderer's GPU half builds only with the client (AUGUSTA_CLIENT), but the
# geometry it builds on the CPU (vertices.h) is free of any GPU API, private to
# the module, so it is tested in every build through a private include path.
add_executable(augusta_renderer_vertices_tests renderer_vertices_test.cpp)
target_include_directories(augusta_renderer_vertices_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(augusta_renderer_vertices_tests PRIVATE augusta_renderer_vertices GTest::gtest GTest::gtest_main)
gtest_discover_tests(augusta_renderer_vertices_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS renderer)

# And which Vulkan device it draws with (vulkan_device_choice.h).
add_executable(augusta_renderer_vulkan_device_choice_tests renderer_vulkan_device_choice_test.cpp)
target_include_directories(augusta_renderer_vulkan_device_choice_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/renderer)
target_link_libraries(
  augusta_renderer_vulkan_device_choice_tests
  PRIVATE augusta_renderer_vulkan_device_choice GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_renderer_vulkan_device_choice_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS renderer)

# And the files its GPU crash dumps are written to (gpu_crash_dump_files.h).
add_executable(augusta_renderer_gpu_crash_dump_files_tests renderer_gpu_crash_dump_files_test.cpp)
target_link_libraries(
  augusta_renderer_gpu_crash_dump_files_tests
  PRIVATE augusta_renderer_gpu_crash_dump_files GTest::gtest GTest::gtest_main
)
gtest_discover_tests(augusta_renderer_gpu_crash_dump_files_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS renderer)

if(AUGUSTA_CLIENT)
  # The client Runtime's internal targets build with the client, on Windows
  # and Linux - though only the smoke test below links the Renderer. Its
  # private headers are reached through its directory, as the renderer's are.
  add_executable(augusta_client_scene_tests client_scene_test.cpp)
  target_link_libraries(augusta_client_scene_tests PRIVATE augusta_client_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_scene_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  add_executable(augusta_client_character_tests client_character_test.cpp)
  target_link_libraries(augusta_client_character_tests PRIVATE augusta_client_content GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_character_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  add_executable(augusta_client_frame_mapping_tests client_frame_mapping_test.cpp)
  target_link_libraries(
    augusta_client_frame_mapping_tests
    PRIVATE augusta_client_frame_mapping GTest::gtest GTest::gtest_main
  )
  target_include_directories(augusta_client_frame_mapping_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
  # Links networking and PhysX through the harness, so the same first-launch
  # cost applies as above.
  gtest_discover_tests(augusta_client_frame_mapping_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  add_executable(augusta_client_lobby_readiness_tests client_lobby_readiness_test.cpp)
  target_link_libraries(
    augusta_client_lobby_readiness_tests
    PRIVATE augusta_client_lobby_readiness GTest::gtest GTest::gtest_main
  )
  target_include_directories(augusta_client_lobby_readiness_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
  gtest_discover_tests(augusta_client_lobby_readiness_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  # The files a Run leaves (see run_files.h), over a temporary directory.
  add_executable(augusta_client_run_files_tests client_run_files_test.cpp)
  target_link_libraries(augusta_client_run_files_tests PRIVATE augusta_client_run_files GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_run_files_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  # What a launch tells the player of the Run before it, and the Session in
  # the log once admitted (see client_run.h).
  add_executable(augusta_client_run_tests client_run_test.cpp)
  target_link_libraries(
    augusta_client_run_tests
    PRIVATE
      augusta_client_run
      augusta_client_run_files
      augusta_logging
      augusta_renderer_gpu_crash_dump_files
      GTest::gtest
      GTest::gtest_main
  )
  gtest_discover_tests(augusta_client_run_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  # augustac as built runs on mimalloc (ADR-0078), as augustad does: it checks
  # the allocator before it reads its command line, so its --version exits 0
  # only when mimalloc serves it, a broken link or redirect failing it. Not
  # under a sanitizer, whose own allocator serves.
  if(NOT AUGUSTA_SANITIZER_ALLOCATOR)
    add_test(NAME augustac_version COMMAND augustac --version)
    set_tests_properties(augustac_version PROPERTIES LABELS client)
  endif()

  add_executable(augusta_client_phases_tests client_phases_test.cpp)
  target_link_libraries(
    augusta_client_phases_tests
    PRIVATE
      augusta_client_phases
      augusta_client_config
      augusta_config
      augusta_failure
      augusta_harness
      GTest::gtest
      GTest::gtest_main
  )
  target_compile_definitions(
    augusta_client_phases_tests
    PRIVATE AUGUSTA_EXAMPLE_PACKS="${CMAKE_SOURCE_DIR}/tests/fixtures/example-packs"
  )
  gtest_discover_tests(augusta_client_phases_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

  add_executable(augusta_client_net_stats_tests client_net_stats_test.cpp)
  target_link_libraries(augusta_client_net_stats_tests PRIVATE augusta_client_net_stats GTest::gtest GTest::gtest_main)
  target_include_directories(augusta_client_net_stats_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
  # NOMINMAX, as augusta_client_net_stats has it: net_stats.h's includes pull in
  # <windows.h>, whose min/max macros would break the headers included after it.
  target_compile_definitions(augusta_client_net_stats_tests PRIVATE NOMINMAX)
  gtest_discover_tests(augusta_client_net_stats_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)

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
  elseif(APPLE)
    # On macOS lavapipe is Homebrew's (its mesa, which scripts/bootstrap/macos.sh
    # installs), under the prefix cmake/LlvmMacosToolchain.cmake finds Homebrew
    # in, through Homebrew's loader, as the client itself runs. Its window
    # needs no virtual display: macOS always has one. The layer's manifest
    # names its library without a folder, as on Linux: vcpkg's lib, put on
    # the dynamic linker's path.
    find_file(
      AUGUSTA_LAVAPIPE_ICD
      NAMES lvp_icd.json lvp_icd.aarch64.json lvp_icd.arm64.json
      PATHS "${augusta_homebrew}/share/vulkan/icd.d"
      NO_DEFAULT_PATH
      DOC "Homebrew's lavapipe ICD manifest, which the renderer smoke test draws on"
    )
    set(no_smoke_test_reason "no lavapipe in ${augusta_homebrew}/share/vulkan/icd.d (Homebrew's mesa)")
    if(AUGUSTA_LAVAPIPE_ICD)
      set(
        smoke_test_environment
        "VK_DRIVER_FILES=${AUGUSTA_LAVAPIPE_ICD};VK_ADD_LAYER_PATH=${vcpkg_installed}/share/vulkan/explicit_layer.d;VK_LOADER_DEBUG=error,warn;--modify;DYLD_LIBRARY_PATH=path_list_prepend:${vcpkg_installed}/lib"
      )
    endif()
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
    gtest_discover_tests(augusta_client_renderer_smoke_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS renderer)
  else()
    message(STATUS "Renderer smoke test not built: ${no_smoke_test_reason}")
  endif()

  # The window's GLFW key mapping (glfw_keys.h), private to the module. The
  # test names keys by GLFW's own constants.
  find_package(glfw3 CONFIG REQUIRED)
  add_executable(augusta_client_window_glfw_keys_tests client_window_glfw_keys_test.cpp)
  target_include_directories(augusta_client_window_glfw_keys_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/window)
  target_link_libraries(
    augusta_client_window_glfw_keys_tests
    PRIVATE augusta_window glfw GTest::gtest GTest::gtest_main
  )
  gtest_discover_tests(augusta_client_window_glfw_keys_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS window)

  # The window itself, hidden, which needs a display: on Linux CI's runner gives
  # it a virtual X server, as the renderer smoke test's (nightly-jobs.yml). It
  # calls the window's GLFW callbacks as the OS's events would, on the GLFW
  # window internals.h, private to the module, gives it.
  add_executable(augusta_client_window_tests client_window_test.cpp)
  target_include_directories(augusta_client_window_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/window)
  target_link_libraries(augusta_client_window_tests PRIVATE augusta_window glfw GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_client_window_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS window)
endif()

# Before the build's kind decides below which sources its fuzz seeds test takes.
add_subdirectory(fuzz)

# Which tests a build's kind carries (ADR-0074), decided here and nowhere else:
# a debug tool's tests only in a Development build, which carries the tool; in
# a Shipping build, which carries none, the one test that the config readers
# refuse a `debug` section. A debug tool's cases for a module both kinds test
# sit in a file of their own, added to that module's test executable here, so
# no test file leaves a case out by `#if AUGUSTA_DEBUG_TOOLS`.
if(AUGUSTA_DEBUG_TOOLS)
  target_sources(augusta_networking_tests PRIVATE networking_tap_test.cpp)
  target_sources(augusta_client_config_tests PRIVATE client_config_debug_test.cpp)
  target_sources(augusta_server_config_tests PRIVATE server_config_debug_test.cpp)
  target_sources(augusta_server_application_tests PRIVATE server_application_debug_test.cpp)
  target_sources(augusta_match_tests PRIVATE match_reenactment_test.cpp)
  target_sources(
    augusta_session_tests
    PRIVATE
      session_reenactment_test.cpp
      session_debug_subscribe_test.cpp
      session_shot_judgement_test.cpp
      session_tap_test.cpp
  )
  # The pcapng writer each tapped end of session_tap_test.cpp writes through.
  target_link_libraries(augusta_session_tests PRIVATE augusta_pcapng augusta_version)
  target_sources(augusta_protocol_tests PRIVATE protocol_debug_test.cpp)
  target_sources(augusta_protocol_property_tests PRIVATE protocol_property_debug_test.cpp)
  target_sources(augusta_link_messages_tests PRIVATE link_messages_debug_test.cpp)
  target_sources(augusta_link_transport_tests PRIVATE link_transport_debug_test.cpp)
  target_sources(augusta_host_metrics_tests PRIVATE host_metrics_debug_test.cpp)
  target_sources(augusta_tick_messages_tests PRIVATE tick_messages_debug_test.cpp)
  target_sources(augusta_replay_server_tests PRIVATE replay_server_debug_test.cpp)
  target_sources(augusta_protocol_decode_seeds_tests PRIVATE fuzz/protocol_decode_seeds_debug.cpp)
  target_sources(augusta_physics_tests PRIVATE physics_debug_test.cpp)
  # OmniPVD's runtime beside it in a Debug build (cmake/PvdRuntime.cmake), so
  # its recording test records where augustac and augustad can.
  augusta_stage_pvd_runtime(augusta_physics_tests)
  target_sources(augusta_prediction_tests PRIVATE prediction_debug_test.cpp)
  if(TARGET augusta_client_frame_mapping_tests)
    target_sources(augusta_client_frame_mapping_tests PRIVATE client_frame_mapping_debug_test.cpp)
  endif()
  target_sources(augusta_simulation_tests PRIVATE simulation_shot_judgement_test.cpp)
  if(TARGET augusta_client_character_tests)
    target_sources(augusta_client_character_tests PRIVATE client_character_debug_test.cpp)
  endif()
  target_link_libraries(augusta_simulation_tests PRIVATE augusta_limits)
  target_sources(augusta_renderer_gpu_crash_dump_files_tests PRIVATE renderer_gpu_crash_dump_files_debug_test.cpp)
  if(TARGET augusta_client_net_stats_tests)
    target_sources(augusta_client_net_stats_tests PRIVATE client_net_stats_debug_test.cpp)
  endif()
  # The Profiling Debug panel (debug_profiling_panel.h), private to the client
  # Runtime, as net_stats.h is.
  if(TARGET augusta_client_debug_profiling_panel)
    add_executable(augusta_client_debug_profiling_panel_tests client_debug_profiling_panel_test.cpp)
    target_link_libraries(
      augusta_client_debug_profiling_panel_tests
      PRIVATE augusta_client_debug_profiling_panel GTest::gtest GTest::gtest_main
    )
    target_include_directories(augusta_client_debug_profiling_panel_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
    gtest_discover_tests(augusta_client_debug_profiling_panel_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)
  endif()
  # Which debug tools the client's Debug index lists (debug_index_entries.h),
  # private to the client Runtime, as debug_profiling_panel.h is.
  if(TARGET augusta_client_debug_index_entries)
    add_executable(augusta_client_debug_index_entries_tests client_debug_index_entries_test.cpp)
    target_link_libraries(
      augusta_client_debug_index_entries_tests
      PRIVATE
        augusta_client_debug_index_entries
        augusta_client_debug_network_panel
        augusta_client_debug_profiling_panel
        augusta_presentation
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(augusta_client_debug_index_entries_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
    # Links PhysX through augusta_presentation, as augusta_tracers_tests does.
    gtest_discover_tests(augusta_client_debug_index_entries_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)
  endif()
  # The Network Debug panel (debug_network_panel.h), private to the client
  # Runtime, as net_stats.h is.
  if(TARGET augusta_client_debug_network_panel)
    add_executable(augusta_client_debug_network_panel_tests client_debug_network_panel_test.cpp)
    target_link_libraries(
      augusta_client_debug_network_panel_tests
      PRIVATE augusta_client_debug_network_panel GTest::gtest GTest::gtest_main
    )
    target_include_directories(augusta_client_debug_network_panel_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/client)
    gtest_discover_tests(augusta_client_debug_network_panel_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS client)
  endif()
  if(TARGET augusta_client_renderer_smoke_tests)
    target_sources(
      augusta_client_renderer_smoke_tests
      PRIVATE
        client_renderer_debug_hud_smoke_test.cpp
        client_renderer_debug_shapes_smoke_test.cpp
        client_renderer_debug_panels_smoke_test.cpp
    )
  endif()

  # What the DebugHud shows (augusta_hud), free of any GPU API.
  add_executable(augusta_hud_tests hud_test.cpp)
  target_link_libraries(augusta_hud_tests PRIVATE augusta_hud GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_hud_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS hud)

  # The Debug overlays' shapes and their tessellation, the Debug index and
  # Debug interaction (augusta_debug), free of any GPU API.
  add_executable(augusta_debug_tests debug_test.cpp debug_index_test.cpp debug_interaction_test.cpp)
  target_link_libraries(augusta_debug_tests PRIVATE augusta_debug GTest::gtest GTest::gtest_main)
  gtest_discover_tests(augusta_debug_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS debug)

  # The Physics Debug overlays, their panel and their shapes (presentation's
  # physics_overlays.h), free of any GPU API.
  add_executable(augusta_physics_overlays_tests physics_overlays_test.cpp)
  target_link_libraries(augusta_physics_overlays_tests PRIVATE augusta_presentation GTest::gtest GTest::gtest_main)
  # Links augusta_prediction publicly (so PhysX), as augusta_tracers_tests does.
  gtest_discover_tests(augusta_physics_overlays_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS presentation)
  # The pcapng's blocks and its file writer (augusta_pcapng, ADR-0076).
  add_executable(augusta_pcapng_tests pcapng_test.cpp pcapng_file_writer_test.cpp)
  target_link_libraries(augusta_pcapng_tests PRIVATE augusta_pcapng augusta_logging GTest::gtest GTest::gtest_main)
  target_include_directories(augusta_pcapng_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/modules/pcapng)
  gtest_discover_tests(augusta_pcapng_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS pcapng)

  # The Shipping-only test as an object library that nothing builds or runs:
  # only so that its file has a compile command for clang-tidy, which lints
  # against a Development build's.
  add_library(augusta_shipping_config_tests OBJECT EXCLUDE_FROM_ALL shipping_config_test.cpp)
  # The same for the protocol_decode seeds of a Shipping build, which has none
  # of a Development build's messages.
  add_library(augusta_protocol_decode_seeds_shipping OBJECT EXCLUDE_FROM_ALL fuzz/protocol_decode_seeds_shipping.cpp)
  target_link_libraries(augusta_protocol_decode_seeds_shipping PRIVATE augusta_protocol)
else()
  add_executable(augusta_shipping_config_tests shipping_config_test.cpp)
  # Links PhysX through augusta_input, as augusta_client_config_tests does.
  gtest_discover_tests(augusta_shipping_config_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS config)
  target_sources(augusta_protocol_decode_seeds_tests PRIVATE fuzz/protocol_decode_seeds_shipping.cpp)
endif()
target_link_libraries(
  augusta_shipping_config_tests
  PRIVATE augusta_client_config augusta_server_config GTest::gtest GTest::gtest_main
)

# augustad's and augustac's binary hardening (the root CMakeLists.txt, NFR-12),
# read off the executables the build made with LLVM's binary tools, on Linux and
# Windows; macOS's, libc++'s hardening mode alone, leaves nothing in the binary
# to read. A script, so it builds nothing. Each test is in its executable's
# area. LLVM's tools sit beside clang-cl on Windows, and on PATH beside the ones
# cmake/LlvmToolchain.cmake finds on Linux.
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
  get_filename_component(compiler_directory ${CMAKE_CXX_COMPILER} DIRECTORY)
  find_program(AUGUSTA_LLVM_READOBJ llvm-readobj HINTS ${compiler_directory} REQUIRED)
  find_program(AUGUSTA_LLVM_NM llvm-nm HINTS ${compiler_directory} REQUIRED)
  function(augusta_binary_hardening_test executable area)
    add_test(
      NAME ${executable}_binary_hardening
      COMMAND
        ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:${executable}> -DREADOBJ=${AUGUSTA_LLVM_READOBJ} -DNM=${AUGUSTA_LLVM_NM}
        -DSYSTEM=${CMAKE_SYSTEM_NAME} -DOPTIMISED=$<NOT:$<CONFIG:Debug>> -P
        ${CMAKE_CURRENT_SOURCE_DIR}/binary_hardening.cmake
    )
    set_tests_properties(${executable}_binary_hardening PROPERTIES LABELS ${area})
  endfunction()
  augusta_binary_hardening_test(augustad server)
  if(AUGUSTA_CLIENT)
    augusta_binary_hardening_test(augustac client)
  endif()
endif()

# Every executable here and in fuzz/, and in each tool's tests/ (kept next to
# the tool) when AUGUSTA_TOOLS builds the tools: what ctest, the benchmarks and
# the fuzz jobs run (cmake/TestAreas.cmake lists the directories). Each
# directory is EXCLUDE_FROM_ALL, so this is how they get built. Custom targets
# (the seed regenerators) stay out: they rewrite committed files. A tool's
# tests/ turns VCPKG_APPLOCAL_DEPS off at its own top (see below).
add_custom_target(augusta_tests)
include(${CMAKE_SOURCE_DIR}/cmake/TestAreas.cmake)
augusta_test_listfiles(${CMAKE_SOURCE_DIR} test_listfiles)
if(NOT AUGUSTA_TOOLS)
  list(FILTER test_listfiles EXCLUDE REGEX "^tools/")
endif()
set(test_directories "")
foreach(listfile IN LISTS test_listfiles)
  get_filename_component(directory ${CMAKE_SOURCE_DIR}/${listfile} DIRECTORY)
  list(APPEND test_directories ${directory})
endforeach()

# On Windows vcpkg gives each executable a POST_BUILD step, z-applocal
# (VCPKG_APPLOCAL_DEPS), that runs vcpkg.exe to copy the DLLs it loads from the
# triplet's bin beside it. When dozens of test executables link side by side,
# some of those launches of vcpkg.exe fail before it starts ("Access is
# denied.", "The process cannot access the file because it is being used by
# another process."), even with every DLL already in place: on a 64-core host,
# 8 of 9 rebuilds of augusta_tests from an empty tests/ failed, 1 to 3 targets
# each, after the DLLs had been copied first (#1306). Launching vcpkg.exe 64 at
# a time, with no build, fails the same way, except from one path: likely an
# antivirus scan at each launch that an exclusion skips (not confirmed). So
# tests/ and each tool's tests/ turn z-applocal off at their top
# (VCPKG_APPLOCAL_DEPS; fuzz/ inherits it), and augusta_test_dlls copies every
# DLL of the triplet's bin into each test directory instead, before any test
# executable builds. The whole bin, not
# what the tests load, so that no list goes stale when a dependency is added.
set(copy_test_dlls OFF)
if(WIN32)
  set(copy_test_dlls ON)
  add_custom_target(augusta_test_dlls COMMENT "Copying vcpkg's DLLs into the test directories")
endif()

foreach(directory IN LISTS test_directories)
  if(copy_test_dlls)
    get_property(binary_directory DIRECTORY ${directory} PROPERTY BINARY_DIR)
    add_custom_command(
      TARGET augusta_test_dlls
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} "-DBIN_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin"
        -DDESTINATION=${binary_directory} -P ${CMAKE_SOURCE_DIR}/cmake/CopyDlls.cmake
      VERBATIM
    )
  endif()
  get_property(directory_targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS directory_targets)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
      add_dependencies(augusta_tests ${target})
      if(copy_test_dlls)
        add_dependencies(${target} augusta_test_dlls)
      endif()
    endif()
  endforeach()
endforeach()

# Each area's executables build alone through augusta_tests_<area>, made from
# the area labels their gtest_discover_tests calls carry (cmake/TestAreas.cmake),
# so labelling an executable is all that adding it to an area takes. The areas
# go to test_areas.txt, from which `make test TAGS=<area>` picks the targets.
augusta_test_area_vocabulary(${CMAKE_SOURCE_DIR} area_vocabulary)
set(test_areas "")
foreach(directory IN LISTS test_directories)
  augusta_read_test_labels(${directory}/CMakeLists.txt discovered)
  foreach(target IN LISTS discovered_TARGETS)
    # A call in an if() branch this build doesn't take names no executable.
    if(NOT TARGET ${target})
      continue()
    endif()
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "EXECUTABLE")
      continue()
    endif()
    augusta_test_label_areas("${discovered_LABELS_${target}}" "${area_vocabulary}" areas unknown)
    foreach(area IN LISTS areas)
      if(NOT TARGET augusta_tests_${area})
        add_custom_target(augusta_tests_${area})
        list(APPEND test_areas ${area})
      endif()
      add_dependencies(augusta_tests_${area} ${target})
    endforeach()
  endforeach()
endforeach()
list(JOIN test_areas " " test_areas)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_areas.txt "${test_areas}")

# The Host: the server's end of the Link and SimulationWorld without threads or
# a clock, the counterpart of the client's Harness (ADR-0060). It speaks to the
# Link alone (augusta/link.h, ADR-0038): no target here links the protocol or
# networking, which checks/core_boundary.cmake checks. The bookkeeping it keeps
# around a Match is pure, so each piece is its own target here and a test drives
# it without a network.

# Who is in the match and whether a join is admitted (see augusta/match.h).
add_library(augusta_host_match STATIC match.cpp)
target_include_directories(augusta_host_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_match PUBLIC augusta_assets augusta_link augusta_math augusta_physics)
augusta_set_module_scope(augusta_host_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move (see
# augusta/command_queue.h).
add_library(augusta_host_commands STATIC command_queue.cpp)
target_include_directories(augusta_host_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_commands PUBLIC augusta_gameplay augusta_link_types)
augusta_set_module_scope(augusta_host_commands SERVER_ONLY)

# The count of each peer's misbehaviour that decides when it is disconnected
# (see augusta/misbehaviour.h).
add_library(augusta_host_misbehaviour STATIC misbehaviour.cpp)
target_include_directories(augusta_host_misbehaviour PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_host_misbehaviour SERVER_ONLY)

# When a peer that has not been admitted is past its deadline (see
# augusta/admission.h).
add_library(augusta_host_admission STATIC admission.cpp)
target_include_directories(augusta_host_admission PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_admission PUBLIC augusta_link)
augusta_set_module_scope(augusta_host_admission SERVER_ONLY)

# The counts behind the server's once-a-second heartbeat line (see
# augusta/heartbeat.h).
add_library(augusta_host_heartbeat STATIC heartbeat.cpp)
target_include_directories(augusta_host_heartbeat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_host_heartbeat SERVER_ONLY)

# The Host itself: the server's end of the Link and SimulationWorld, driven by
# hand (see augusta/host.h), and the conversions it makes at its edges - into
# the Link's messages (see augusta/link_mapping.h) and into SimulationWorld's (see
# augusta/simulation_mapping.h) - with the Match capture it writes (see
# augusta/capturer.h, ADR-0050) and what it counts for the metrics endpoint (see
# augusta/host_metrics.h, ADR-0049).
find_package(prometheus-cpp CONFIG REQUIRED)
add_library(
  augusta_host
  STATIC
  capture_retention.cpp
  capturer.cpp
  host.cpp
  host_log.cpp
  host_match.cpp
  host_metrics.cpp
  host_network.cpp
  link_mapping.cpp
  peer_gate.cpp
  replay.cpp
  replay_catalog.cpp
  simulation_mapping.cpp
  tick_messages.cpp
)
target_include_directories(augusta_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_host
  PUBLIC
    augusta_failure
    augusta_gameplay
    augusta_link
    augusta_physics
    augusta_simulation
    augusta_scenario
    augusta_policy_loader
    augusta_host_commands
    augusta_host_match
    augusta_host_misbehaviour
    augusta_host_admission
    augusta_host_heartbeat
    augusta_assets
    augusta_capture
    augusta_metrics
    augusta_uuid
    prometheus-cpp::core
  PRIVATE augusta_logging augusta_version
)
augusta_set_module_scope(augusta_host SERVER_ONLY)

# The Replay server (see augusta/replay_server.h, ADR-0051): it serves a
# recorded Match to Replay viewers through a Link endpoint of its own, with the
# Host's own log, metrics and peer gate, so it moves with the Host; splitting it
# out is a later decision.
add_library(augusta_host_replay STATIC replay_server.cpp)
target_include_directories(augusta_host_replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_host_replay PUBLIC augusta_host PRIVATE augusta_logging augusta_version)
augusta_set_module_scope(augusta_host_replay SERVER_ONLY)

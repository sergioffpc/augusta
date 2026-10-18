add_library(
  augusta_harness
  STATIC
  capture_script.cpp
  command_stream.cpp
  harness.cpp
  inbox.cpp
  intent.cpp
  netcode_stats.cpp
  reenactment.cpp
  replay_list.cpp
  runner.cpp
  server_view.cpp
)
target_include_directories(augusta_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_harness
  PUBLIC
    augusta_assets
    augusta_gameplay
    augusta_math
    augusta_prediction
    augusta_failure
    augusta_physics
    augusta_capture
    augusta_supervisor
    # The Link alone: it owns the transport and the protocol beneath, which the
    # harness never links (ADR-0038, tests/core_boundary.cmake).
    augusta_link
    augusta_version
  # nvtx3-cpp: runner.cpp names the Prediction and Network I/O loops for Nsight Systems.
  # augusta_input: intent.cpp aims within the pitch a player may look at.
  PRIVATE augusta_input augusta_logging nvtx3-cpp
)
augusta_set_module_scope(augusta_harness CLIENT_ONLY)

add_library(
  augusta_harness
  STATIC
  command_stream.cpp
  harness.cpp
  harness_wire.cpp
  inbox.cpp
  runner.cpp
  server_view.cpp
)
target_include_directories(augusta_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_harness
  PUBLIC
    augusta_assets
    augusta_ballistics
    augusta_math
    augusta_networking
    augusta_parameters
    augusta_prediction
    augusta_command
    augusta_failure
    augusta_physics
    augusta_protocol
    augusta_supervisor
    augusta_tick
    augusta_version
    augusta_weapon
  # nvtx3-cpp: runner.cpp names the Prediction and Network I/O loops for Nsight Systems.
  PRIVATE augusta_logging augusta_primitives nvtx3-cpp
)
augusta_set_module_scope(augusta_harness CLIENT_ONLY)

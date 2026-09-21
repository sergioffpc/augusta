# Who is in the match and whether a join is admitted; pure, so tested without a
# network (see match.h).
add_library(augustad_match STATIC
  match.cpp
)
target_include_directories(augustad_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_match PUBLIC augusta_networking augusta_protocol)
augusta_set_module_scope(augustad_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move; pure, so
# tested without a network (see command_queue.h).
add_library(augustad_commands STATIC
  command_queue.cpp
)
target_include_directories(augustad_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_commands PUBLIC augusta_input augusta_protocol)
augusta_set_module_scope(augustad_commands SERVER_ONLY)

# The server's socket and SimulationWorld without threads or a clock, so a test
# can drive a match by hand (see host.h).
add_library(augustad_host STATIC
  host.cpp
)
target_include_directories(augustad_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_host
  PUBLIC augusta_networking augusta_parameters augusta_parameters_loader augusta_physics augusta_simulation
  PRIVATE augustad_commands augustad_match augusta_logging augusta_protocol augusta_replication augusta_version
)
augusta_set_module_scope(augustad_host SERVER_ONLY)

add_library(augustad_runtime STATIC
  runtime.cpp
)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_runtime
  PUBLIC augustad_host augusta_networking augusta_parameters augusta_physics augusta_simulation
  PRIVATE augusta_assets augusta_input augusta_logging
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

add_executable(augustad main.cpp)
target_link_libraries(augustad PRIVATE augusta_assets augusta_config augusta_map augusta_parameters_loader augusta_version augusta_logging augusta_networking augustad_runtime)

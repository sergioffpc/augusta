# Who is in the match and whether a join is admitted; pure, so tested without a
# network (see match.h).
add_library(augustad_match STATIC
  match.cpp
)
target_include_directories(augustad_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_match PUBLIC augusta_networking augusta_protocol)
augusta_set_module_scope(augustad_match SERVER_ONLY)

# The server's socket and SimulationWorld without threads or a clock, so a test
# can drive a match by hand (see host.h).
add_library(augustad_host STATIC
  host.cpp
)
target_include_directories(augustad_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_host
  PUBLIC augusta_networking augusta_physics augusta_simulation
  PRIVATE augustad_match augusta_input augusta_logging augusta_protocol augusta_version
)
augusta_set_module_scope(augustad_host SERVER_ONLY)

add_library(augustad_runtime STATIC
  runtime.cpp
)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_runtime
  PUBLIC augustad_host augusta_networking augusta_physics augusta_simulation
  PRIVATE augusta_assets augusta_input augusta_logging
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

add_executable(augustad main.cpp)
target_link_libraries(augustad PRIVATE augusta_assets augusta_config augusta_map augusta_version augusta_logging augusta_networking augustad_runtime)

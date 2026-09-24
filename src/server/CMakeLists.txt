# Reads the Parameters script through Lua (ADR-0022, ADR-0039) into the shared
# Parameters; pure, so tested with scripts as strings (see parameters_loader.h).
# Server-only: a client never reads the script, it is sent the result, so it
# never links the interpreter.
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(augustad_parameters_loader STATIC
  parameters_loader.cpp
)
target_include_directories(augustad_parameters_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(augustad_parameters_loader SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(augustad_parameters_loader
  PUBLIC augusta_parameters
  PRIVATE sol2::sol2 ${LUA_LIBRARIES}
)
augusta_set_module_scope(augustad_parameters_loader SERVER_ONLY)

# Who is in the match and whether a join is admitted; pure, so tested without a
# network (see match.h).
add_library(augustad_match STATIC
  match.cpp
)
target_include_directories(augustad_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_match PUBLIC augusta_assets augusta_math augusta_networking augusta_physics)
augusta_set_module_scope(augustad_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move; pure, so
# tested without a network (see command_queue.h).
add_library(augustad_commands STATIC
  command_queue.cpp
)
target_include_directories(augustad_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_commands PUBLIC augusta_input)
augusta_set_module_scope(augustad_commands SERVER_ONLY)

# The server's socket and SimulationWorld without threads or a clock, so a test
# can drive a match by hand (see host.h), and the conversions between the
# protocol's plain types and the engine's that Host makes at the edge (see wire.h).
add_library(augustad_host STATIC
  host.cpp
  wire.cpp
)
target_include_directories(augustad_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_host
  PUBLIC augusta_networking augusta_parameters augusta_physics augusta_simulation augustad_parameters_loader
         augustad_commands augustad_match augusta_assets augusta_protocol augusta_replication augusta_tick
  PRIVATE augusta_logging augusta_version
)
augusta_set_module_scope(augustad_host SERVER_ONLY)

add_library(augustad_runtime STATIC
  runtime.cpp
)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_runtime
  PUBLIC augustad_host augusta_networking augusta_parameters augusta_physics augusta_simulation
  PRIVATE augusta_assets augusta_input augusta_logging augusta_tick
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

add_executable(augustad main.cpp)
target_link_libraries(augustad PRIVATE augusta_assets augusta_config augusta_map augustad_parameters_loader augusta_version augusta_logging augusta_networking augustad_runtime)

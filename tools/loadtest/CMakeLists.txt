# augusta-loadtest: the Scripted players (CONTEXT.md), a whole server's worth of
# clients with no window or GPU, for load and end-to-end tests. A tool, built
# only with AUGUSTA_LOADTEST (the root CMakeLists.txt), on every platform.

# Its startup settings, read under ADR-0034's rules (see settings.h).
add_library(augusta_loadtest_settings STATIC
  settings.cpp
)
target_include_directories(augusta_loadtest_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_loadtest_settings PUBLIC augusta_config)
augusta_set_module_scope(augusta_loadtest_settings CLIENT_ONLY)

# What a Scripted player does each tick (see scripted_player.h): the decision
# only, so its tests need no connection.
add_library(augusta_loadtest_scripted_player STATIC
  scripted_player.cpp
)
target_include_directories(augusta_loadtest_scripted_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_loadtest_scripted_player
  PUBLIC augusta_command augusta_harness augusta_physics
  PRIVATE augusta_input augusta_math augusta_weapon
)
augusta_set_module_scope(augusta_loadtest_scripted_player CLIENT_ONLY)

# A run of Scripted players against a server, to its verdict (see run.h).
add_library(augusta_loadtest_run STATIC
  run.cpp
)
target_include_directories(augusta_loadtest_run PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_loadtest_run
  PUBLIC augusta_harness augusta_physics
  PRIVATE augusta_loadtest_scripted_player augusta_logging augusta_prediction augusta_supervisor
)
augusta_set_module_scope(augusta_loadtest_run CLIENT_ONLY)

add_executable(augusta-loadtest main.cpp)
target_link_libraries(augusta-loadtest PRIVATE augusta_loadtest_run augusta_loadtest_settings augusta_assets augusta_config augusta_logging augusta_map augusta_networking augusta_version)
if (WIN32)
  # winmm: main.cpp raises the system timer resolution (timeBeginPeriod), as
  # augustac does. NOMINMAX: the <windows.h> it includes would define min/max.
  target_link_libraries(augusta-loadtest PRIVATE winmm)
  target_compile_definitions(augusta-loadtest PRIVATE NOMINMAX)
endif()

# Its tests build only on request, through augusta_tests (tests/CMakeLists.txt),
# as the engine's do, and ctest runs them in a build that has the tool.
add_subdirectory(tests EXCLUDE_FROM_ALL)

# augusta-replay: replays a match recording augustad wrote against the server
# pack it names (ADR-0048). A developer's tool, not shipped with the server,
# built with AUGUSTA_TOOLS (the root CMakeLists.txt).

# The replay itself, apart from the command line (see replay.h): its tests run
# it on recordings made in the test.
add_library(augusta_replay STATIC
  replay.cpp
)
target_include_directories(augusta_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_replay
  PUBLIC augustad_content augustad_host augusta_simulation augusta_tick
  PRIVATE augusta_math
)
augusta_set_module_scope(augusta_replay SERVER_ONLY)

add_executable(augusta-replay main.cpp)
target_link_libraries(augusta-replay PRIVATE augusta_replay augusta_assets augusta_logging augusta_version)

# Its tests build only on request, through augusta_tests (tests/CMakeLists.txt),
# as the engine's do.
add_subdirectory(tests EXCLUDE_FROM_ALL)

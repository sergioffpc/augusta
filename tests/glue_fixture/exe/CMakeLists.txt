# One executable's own directory: the executable alone, linking the application,
# its Runtime, failure, logging and version, and a system library that is no
# target. CASE adds a second target beside it, or a link it may not have.
add_library(fixture_exe INTERFACE)
target_link_libraries(
  fixture_exe
  INTERFACE
    augusta_application
    augusta_failure
    augusta_logging
    augusta_version
    fixture_runtime
    fixture_runtime_application
    winmm
)

if(CASE STREQUAL "EXTRA_TARGET")
  add_library(fixture_exe_runtime INTERFACE)
elseif(CASE STREQUAL "LINK_REFUSED")
  target_link_libraries(fixture_exe INTERFACE fixture_exe_module)
endif()

augusta_set_executable_glue(fixture_exe fixture_runtime)

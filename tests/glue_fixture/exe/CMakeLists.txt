# One executable's own directory: its Runtime, every target in it, its
# application among them, as src/client and src/server hold augustac's and
# augustad's; then the executable, linking the application, its Runtime,
# failure, logging and version, the build infrastructure every binary links,
# and a system library that is no target. CASE adds a link it may not have.
add_library(fixture_runtime INTERFACE)
add_library(fixture_runtime_application INTERFACE)

add_library(fixture_exe INTERFACE)
target_link_libraries(
  fixture_exe
  INTERFACE
    augusta_application
    augusta_failure
    augusta_logging
    augusta_version
    fixture_every_binary
    fixture_runtime
    fixture_runtime_application
    winmm
)

if(CASE STREQUAL "LINK_REFUSED")
  target_link_libraries(fixture_exe INTERFACE fixture_exe_module)
endif()

augusta_set_executable_glue(fixture_exe)

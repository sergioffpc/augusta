# One executable's own directory: its entry point, its application and its
# runtime are Glue, and CASE decides whether a fourth library sits beside them.
# Every target is an INTERFACE library so the fixture needs no compiler; the
# check reads a target's type and its source directory, not how it is built.
add_library(fixture_exe INTERFACE)
add_library(fixture_exe_application INTERFACE)
add_library(fixture_exe_runtime INTERFACE)

if(CASE MATCHES "^(VIOLATION|EXCEPTED)$")
  add_library(fixture_exe_mechanism INTERFACE)
endif()

augusta_set_executable_glue(fixture_exe)

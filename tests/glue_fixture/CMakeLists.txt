# A build graph for glue_boundary.cmake to configure: whether the Glue check
# (cmake/AugustaModule.cmake) refuses a library target defined inside an
# executable's own directory. CASE picks the graph: GLUE (only the
# executable's own three), VIOLATION (a fourth library beside them), EXCEPTED
# (that fourth library, named as an exception), STALE (an exception naming a
# target that has already moved out) or UNBUILT (the executable left out of
# this build altogether).
cmake_minimum_required(VERSION 3.25)
project(augusta_glue_fixture NONE)

include(${AUGUSTA_SOURCE_DIR}/cmake/AugustaModule.cmake)

# The exception ledger the check reads, in place of the one the repo's own
# executables seed it with.
if(CASE MATCHES "^(EXCEPTED|STALE|UNBUILT)$")
  set(AUGUSTA_GLUE_EXCEPTIONS_fixture_exe fixture_exe_mechanism)
endif()

# A library outside the executable's directory, named as if it belonged to it:
# the rule judges a target by where it is defined, not by what it is called,
# so this one is allowed in every case.
add_library(fixture_exe_module INTERFACE)

# What a build that leaves an executable out does: its directory is never
# added, so it never registers itself.
if(NOT CASE STREQUAL "UNBUILT")
  add_subdirectory(exe)
endif()

augusta_check_executable_glue()

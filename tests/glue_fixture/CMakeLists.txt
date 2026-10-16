# A build graph for glue_boundary.cmake to configure: whether the Glue check
# (cmake/AugustaModule.cmake) refuses a library target defined inside an
# executable's own directory, and a test target that links Glue it may not.
# CASE picks the graph: GLUE (only the
# executable's own three), VIOLATION (a fourth library beside them), EXCEPTED
# (that fourth library, named as an exception), STALE (an exception naming a
# target that has already moved out), UNBUILT (the executable left out of this
# build altogether), TEST_UNIT (a test target that links the executable's
# runtime without declaring itself an end-to-end test), TEST_END_TO_END (the
# same test, declared), TEST_FOREIGN (a declared test reaching across to a
# second executable's runtime), TEST_EXECUTABLE (a declared test linking the
# executable itself), TEST_EXCEPTED (an undeclared test named as an exception)
# or TEST_STALE (that exception, once the test links a module instead).
cmake_minimum_required(VERSION 3.25)
project(augusta_glue_fixture NONE)

include(${AUGUSTA_SOURCE_DIR}/cmake/AugustaModule.cmake)

# The exception ledger the check reads, in place of the one the repo's own
# executables seed it with.
if(CASE MATCHES "^(EXCEPTED|STALE|UNBUILT)$")
  set(AUGUSTA_GLUE_EXCEPTIONS_fixture_exe fixture_exe_mechanism)
elseif(CASE MATCHES "^TEST_(EXCEPTED|STALE)$")
  set(AUGUSTA_GLUE_EXCEPTIONS_fixture_exe fixture_test)
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

# A second executable, for the case about a test reaching across to one.
if(CASE STREQUAL "TEST_FOREIGN")
  add_subdirectory(other)
endif()

# The test tree, added only for the cases about what a test may link.
if(CASE MATCHES "^TEST_")
  add_subdirectory(tests)
endif()

augusta_check_executable_glue()

# A build graph for glue_boundary.cmake to configure: whether the Glue check
# (cmake/AugustaModule.cmake) refuses a target defined inside an executable's
# own directory other than the executable, an executable that links more than
# the application, its Runtime, failure, logging and version, and a target that
# links an executable. CASE picks the graph: GLUE (an executable linking all it
# may, build infrastructure every binary links among it, and a test linking its
# Runtime), EXTRA_TARGET (a second target beside the executable, named as its
# runtime), LINK_REFUSED (the executable linking a module it may not) or
# TEST_EXECUTABLE (a test linking the executable itself).
cmake_minimum_required(VERSION 3.25)
project(augusta_glue_fixture NONE)

include(${AUGUSTA_SOURCE_DIR}/cmake/AugustaModule.cmake)

# Stand-ins for the modules an executable may link besides its Runtime. Every
# target is an INTERFACE library so the fixture needs no compiler; the check
# reads a target's name, its source directory and its links, not how it is
# built.
add_library(augusta_application INTERFACE)
add_library(augusta_failure INTERFACE)
add_library(augusta_logging INTERFACE)
add_library(augusta_version INTERFACE)

# Build infrastructure every binary links, as a sanitizer's default options are
# in an ASan build: the executable may link it too. link_libraries() passes an
# INTERFACE library by, so the executable also names it itself.
add_library(fixture_every_binary INTERFACE)
augusta_link_every_binary(fixture_every_binary)

# A module the executable may not link, named as if it belonged to it: the rule
# judges a target by where it is defined, not by what it is called.
add_library(fixture_exe_module INTERFACE)

# The executable's Runtime module, with its application beside it.
add_subdirectory(runtime)
add_subdirectory(exe)
add_subdirectory(tests)

augusta_check_executable_glue()

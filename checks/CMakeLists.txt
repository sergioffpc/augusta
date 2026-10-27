# Checks of the repository's structure rather than of a feature: the
# architecture's boundaries, the Glue rule, the test areas, the platforms
# augustad configures for and the Linux client's staging. Each is a `cmake -P`
# script that builds nothing, so they configure with the rest of the build, not
# behind augusta_tests. The scripts' own pytest tests sit beside them; CI runs
# those.

# No header past the protocol's edge names a protocol type (ADR-0038): only
# the Link converts (augusta/link_messages.h). Nor does a presentation header
# name the harness: the client converts (src/client/frame_mapping.h).
# Nor does the protocol itself include or link anything but augusta_math,
# augusta_tick, augusta_limits, Boost::uuid and the full protobuf runtime,
# PRIVATE, nor does any header outside it include protobuf or the generated
# augusta.pb.h. A source scan, so it builds nothing.
add_test(
  NAME augusta_protocol_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/protocol_boundary.cmake
)

# Neither physics nor SimulationWorld names the protocol or the client's input
# sampler in its public headers: the grids and the Command they need are shared
# core modules of their own. Nor do tick and limits depend on any other module,
# nor does anything else give the Tick or the Sequence a type of its own or
# declare a bound again, even as an alias. Nor do the harness and the host reach
# beneath the Link, nor does capture link more of it than its conversions, nor
# does any target but the Link's own (and the transport's own tests) link
# networking, the last two first proved on a planted violation. A source scan,
# so it builds nothing.
add_test(
  NAME augusta_core_boundary
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/core_boundary.cmake
)

# Which platforms augustad configures for, and as what (NFR-04): Linux x86-64 in
# production, Windows x64 and Linux arm64 for development, nothing else
# (cmake/AugustaPlatform.cmake).
# A script, so it builds nothing.
add_test(
  NAME augusta_server_platform
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/server_platform.cmake
)

# The headless server has no client Input dependency (ARCHITECTURE.md §5):
# augusta_input stays client-only so the configure-time scope check covers it,
# that check rejects a fixture build graph where a server-only target reaches a
# client-only one, and no server source or server config header names Input.
# It only configures that fixture, with no compiler, so it builds nothing.
add_test(
  NAME augusta_server_boundary
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/server_boundary.cmake
)

# Executables are Glue (ADR-0060): the Glue check refuses a second target
# inside an executable's own directory, an executable that links more than the
# application, its Runtime, crash, failure, logging and version, and a test
# that links an executable, and allows an executable linking all of those and a
# test linking its Runtime. It only configures a fixture build graph, with no
# compiler, so it builds nothing.
add_test(
  NAME augusta_glue_boundary
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/glue_boundary.cmake
)

# Every test executable names exactly one area (ADR-0008): the area check
# refuses a gtest_discover_tests call with no area label, with more than one,
# or with a label that is neither a module's, Runtime's or tool's name nor
# check, property or netcode (cmake/TestAreas.cmake), each first proved on a
# planted violation. A source scan, so it builds nothing.
add_test(
  NAME augusta_test_areas
  COMMAND
    ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P
    ${CMAKE_CURRENT_SOURCE_DIR}/test_areas.cmake
)

# What the Linux client's tarball is checked for before a release publishes it
# (scripts/stage-client.sh, ADR-0058): the check itself, against a stand-in for
# augustac's build, so it runs on every pull request although augustac's Linux
# build does not.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(
    NAME augusta_stage_client_linux
    COMMAND
      ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/stage_client_linux.cmake
  )
endif()

# `make check` runs these alone: ctest's check label.
set_tests_properties(
  augusta_protocol_boundary
  augusta_core_boundary
  augusta_server_platform
  augusta_server_boundary
  augusta_glue_boundary
  augusta_test_areas
  PROPERTIES LABELS check
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_tests_properties(augusta_stage_client_linux PROPERTIES LABELS check)
endif()

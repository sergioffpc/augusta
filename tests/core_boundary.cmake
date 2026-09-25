# Keeps the shared core's base below the network layer and the client's device
# input: no public header of physics or SimulationWorld may name a protocol type
# or the input sampler's module, or include either's header. The grids a body
# lives on are augusta::math's (augusta/grid.h) and the Command SimulationWorld consumes is
# augusta::command's, so neither needs them.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -P core_boundary.cmake`.
if (NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "core_boundary.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

file(GLOB headers
  "${SOURCE_DIR}/src/modules/physics/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/simulation/include/augusta/*.h"
)
if (NOT headers)
  message(FATAL_ERROR "core_boundary.cmake: no headers found under ${SOURCE_DIR}")
endif()

# Only a whole name counts: `input::` right after a letter, digit or underscore
# (as in `raw_input::`) is some other name.
set(forbidden "(^|[^A-Za-z0-9_])(protocol|input)::|augusta/(protocol|input)\\.h")

set(violations "")
foreach(header ${headers})
  file(STRINGS "${header}" lines REGEX "${forbidden}")
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    string(STRIP "${line}" line)
    list(APPEND violations "  ${relative}: ${line}")
  endforeach()
endforeach()

if (violations)
  list(JOIN violations "\n" report)
  message(FATAL_ERROR
    "physics or SimulationWorld names the protocol or the input sampler - use augusta/grid.h or augusta::command instead:\n${report}")
endif()
list(LENGTH headers count)
message(STATUS "core_boundary: ${count} headers free of protocol and input types")

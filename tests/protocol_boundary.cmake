# Keeps the Networking Protocol at the edge (ADR-0038): no header of the
# modules past it - the harness's API, the server's Match, Host and command
# queue, replication, presentation, ClientRuntime - may name a protocol type
# or include the protocol's header. Only the two adapters convert:
# harness_wire.h on the client and server/wire.h on the server.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -P protocol_boundary.cmake`.
if (NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "protocol_boundary.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

file(GLOB headers
  "${SOURCE_DIR}/src/modules/harness/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/replication/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/presentation/include/augusta/*.h"
  "${SOURCE_DIR}/src/server/*.h"
  "${SOURCE_DIR}/src/client/*.h"
)
list(REMOVE_ITEM headers
  "${SOURCE_DIR}/src/modules/harness/include/augusta/harness_wire.h"
  "${SOURCE_DIR}/src/server/wire.h"
)
if (NOT headers)
  message(FATAL_ERROR "protocol_boundary.cmake: no headers found under ${SOURCE_DIR}")
endif()

set(violations "")
foreach(header ${headers})
  file(STRINGS "${header}" lines REGEX "protocol::|augusta/protocol\\.h")
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    string(STRIP "${line}" line)
    list(APPEND violations "  ${relative}: ${line}")
  endforeach()
endforeach()

if (violations)
  list(JOIN violations "\n" report)
  message(FATAL_ERROR
    "The protocol leaks past its edge (ADR-0038) - convert in harness_wire.h or server/wire.h instead:\n${report}")
endif()
list(LENGTH headers count)
message(STATUS "protocol_boundary: ${count} headers free of protocol types")

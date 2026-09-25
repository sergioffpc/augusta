# Keeps the Networking Protocol at the edge (ADR-0038): no header of the
# modules past it - the harness's API, the server's Match, Host and command
# queue, replication, presentation, ClientRuntime - may name a protocol type
# or include the protocol's header. Only the two adapters convert:
# harness_wire.h on the client and server/wire.h on the server.
#
# Keeps the harness at ClientRuntime's edge the same way: no presentation
# header may name a harness type or include a harness header, so presentation
# does not depend on the network session. ClientRuntime converts.
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

file(GLOB presentation_headers "${SOURCE_DIR}/src/modules/presentation/include/augusta/*.h")
if (NOT presentation_headers)
  message(FATAL_ERROR "protocol_boundary.cmake: no presentation headers found under ${SOURCE_DIR}")
endif()

set(harness_violations "")
foreach(header ${presentation_headers})
  file(STRINGS "${header}" lines REGEX "harness::|augusta/harness")
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    string(STRIP "${line}" line)
    list(APPEND harness_violations "  ${relative}: ${line}")
  endforeach()
endforeach()

set(report "")
if (violations)
  list(JOIN violations "\n" protocol_report)
  string(APPEND report
    "The protocol leaks past its edge (ADR-0038) - convert in harness_wire.h or server/wire.h instead:\n${protocol_report}\n")
endif()
if (harness_violations)
  list(JOIN harness_violations "\n" harness_report)
  string(APPEND report
    "The harness leaks into presentation - convert to presentation's own types in ClientRuntime instead:\n${harness_report}\n")
endif()
if (report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH headers count)
list(LENGTH presentation_headers presentation_count)
message(STATUS
  "protocol_boundary: ${count} headers free of protocol types, ${presentation_count} presentation headers free of harness types")

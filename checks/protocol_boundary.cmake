# Keeps the Networking Protocol at the edge (ADR-0038): no header of the
# modules past it - the harness's API, the server's Match, Host and command
# queue, simulation's Replication, presentation, ClientRuntime, the capture
# module's interface, the Link's own types - may name a protocol type or include
# the protocol's header. Only the Link converts (augusta/link_messages.h), for
# the client, the server and a Match capture's records alike.
#
# Keeps the harness at ClientRuntime's edge the same way: no presentation
# header may name a harness type or include a harness header, so presentation
# does not depend on the network session. ClientRuntime converts.
#
# Keeps the protocol itself a pure codec below the engine: its sources include
# no augusta header but its own, the math's (augusta/math.h, augusta/grid.h),
# the tick's (augusta/tick.h) and the limits' (augusta/limits.h), and its target
# links nothing else, so a gameplay module (the Command, the Parameters) never
# becomes what the wire depends on. Its links beyond them are the full
# protobuf runtime, never the lite one, and protobuf's utf8_range, both
# PRIVATE (ADR-0007).
#
# Keeps protobuf inside the protocol: no header outside src/modules/protocol
# includes a protobuf header, utf8_range's or the generated augusta.pb.h, so
# protobuf never reaches the engine's vocabulary (ADR-0007).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -P protocol_boundary.cmake`.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "protocol_boundary.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

file(
  GLOB headers
  "${SOURCE_DIR}/src/modules/harness/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/simulation/include/augusta/replication*.h"
  "${SOURCE_DIR}/src/modules/presentation/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/capture/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/link/include/augusta/link_types.h"
  "${SOURCE_DIR}/src/modules/host/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/host/*.h"
  "${SOURCE_DIR}/src/server/include/augusta/*.h"
  "${SOURCE_DIR}/src/server/*.h"
  "${SOURCE_DIR}/src/client/include/augusta/*.h"
  "${SOURCE_DIR}/src/client/*.h"
)
if(NOT headers)
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
if(NOT presentation_headers)
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

file(GLOB_RECURSE protocol_sources "${SOURCE_DIR}/src/modules/protocol/*.h" "${SOURCE_DIR}/src/modules/protocol/*.cpp")
if(NOT protocol_sources)
  message(FATAL_ERROR "protocol_boundary.cmake: no protocol sources found under ${SOURCE_DIR}")
endif()

set(allowed_include "augusta/(protocol|math|grid|tick|limits)\\.h")
set(allowed_link "augusta_(math|tick|limits)")
set(dependency_violations "")
foreach(source ${protocol_sources})
  file(STRINGS "${source}" lines REGEX "#[ \t]*include[ \t]*[\"<]augusta/")
  foreach(line ${lines})
    if(line MATCHES "[\"<]${allowed_include}[\">]")
      continue()
    endif()
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    string(STRIP "${line}" line)
    list(APPEND dependency_violations "  ${relative}: ${line}")
  endforeach()
endforeach()
# The protocol's own target_link_libraries calls: every target they name must
# be one of the allowed three, or the full protobuf runtime or utf8_range under
# PRIVATE.
file(READ "${SOURCE_DIR}/src/modules/protocol/CMakeLists.txt" protocol_cmake)
string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" link_calls "${protocol_cmake}")
foreach(call ${link_calls})
  string(REGEX REPLACE "^target_link_libraries\\(|\\)$" "" arguments "${call}")
  separate_arguments(arguments)
  list(POP_FRONT arguments)
  set(scope "")
  foreach(target ${arguments})
    if(target MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
      set(scope "${target}")
    elseif(target MATCHES "^(protobuf::libprotobuf|utf8_range::utf8_range)$")
      if(NOT scope STREQUAL "PRIVATE")
        list(
          APPEND dependency_violations
          "  src/modules/protocol/CMakeLists.txt: links ${target} ${scope}, not PRIVATE"
        )
      endif()
    elseif(NOT target MATCHES "^${allowed_link}$")
      list(APPEND dependency_violations "  src/modules/protocol/CMakeLists.txt: links ${target}")
    endif()
  endforeach()
endforeach()

# Every C++ header outside the protocol module, in the engine, its tests and the
# tools, skipping hidden directories (a tool's virtual environment).
file(
  GLOB_RECURSE engine_headers
  "${SOURCE_DIR}/src/*.h"
  "${SOURCE_DIR}/src/*.hpp"
  "${SOURCE_DIR}/tests/*.h"
  "${SOURCE_DIR}/tests/*.hpp"
  "${SOURCE_DIR}/tools/*.h"
  "${SOURCE_DIR}/tools/*.hpp"
)
list(FILTER engine_headers EXCLUDE REGEX "^${SOURCE_DIR}/src/modules/protocol/|/\\.[^/]+/")
set(protobuf_violations "")
foreach(header ${engine_headers})
  file(
    STRINGS "${header}"
    lines
    REGEX "#[ \t]*include[ \t]*[\"<](google/protobuf/|utf8_range\\.h|([^\">]*/)?augusta\\.pb\\.h)"
  )
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    string(STRIP "${line}" line)
    list(APPEND protobuf_violations "  ${relative}: ${line}")
  endforeach()
endforeach()

set(report "")
if(violations)
  list(JOIN violations "\n" protocol_report)
  string(
    APPEND report
    "The protocol leaks past its edge (ADR-0038) - convert in augusta/link_messages.h instead:\n${protocol_report}\n"
  )
endif()
if(harness_violations)
  list(JOIN harness_violations "\n" harness_report)
  string(
    APPEND report
    "The harness leaks into presentation - convert to presentation's own types in src/client/frame_mapping.h instead:\n${harness_report}\n"
  )
endif()
if(dependency_violations)
  list(JOIN dependency_violations "\n" dependency_report)
  string(
    APPEND report
    "The protocol depends on a module above it, or on protobuf other than its full runtime and utf8_range linked PRIVATE - take the shared counter or bound from augusta/tick.h or augusta/limits.h instead:\n${dependency_report}\n"
  )
endif()
if(protobuf_violations)
  list(JOIN protobuf_violations "\n" protobuf_report)
  string(
    APPEND report
    "protobuf leaks past the protocol (ADR-0007) - include it, and the generated augusta.pb.h, in src/modules/protocol alone:\n${protobuf_report}\n"
  )
endif()
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH headers count)
list(LENGTH presentation_headers presentation_count)
list(LENGTH engine_headers engine_count)
message(
  STATUS
  "protocol_boundary: ${count} headers free of protocol types, ${presentation_count} presentation headers free of harness types, the protocol on math, tick, limits, the protobuf runtime and utf8_range only, ${engine_count} headers free of protobuf"
)

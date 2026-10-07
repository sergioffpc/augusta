# Keeps client Input off the headless server's surface (ARCHITECTURE.md §5):
# augustad's build graph, its startup configuration and its public interfaces
# depend on server-owned and shared concerns only.
#
# - Linkage: augusta_input is client-only, so the configure-time scope check
#   (cmake/AugustaModule.cmake) rejects any server-only target that links it,
#   directly or through a shared module; a fixture build graph proves the check
#   fires both ways.
# - Source: no server header or source, nor a header of the shared or server
#   config, names the input sampler or the client's config, or includes either.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>
# [-DGENERATOR=<generator>] -P server_boundary.cmake`.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "server_boundary.cmake: pass -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>")
endif()

set(report "")

# The input sampler's module must be declared client-only for the check to cover it.
file(READ "${SOURCE_DIR}/src/modules/input/CMakeLists.txt" input_cmake)
if(NOT input_cmake MATCHES "augusta_set_module_scope\\(augusta_input CLIENT_ONLY\\)")
  string(APPEND report "augusta_input is not declared CLIENT_ONLY (src/modules/input/CMakeLists.txt)\n")
endif()

# The build's own generator when ctest passes it (-DGENERATOR=...), so the
# fixture needs nothing the build doesn't; CMake's platform default otherwise.
set(generator "")
if(GENERATOR)
  set(generator -G "${GENERATOR}")
endif()

# Configures the fixture with edge; sets out_failed and out_output.
function(configure_fixture edge out_failed out_output)
  set(build "${BINARY_DIR}/module_scope_fixture_${edge}")
  file(REMOVE_RECURSE "${build}")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -S "${SOURCE_DIR}/tests/module_scope_fixture" -B "${build}" ${generator} -DEDGE=${edge}
      -DAUGUSTA_SOURCE_DIR=${SOURCE_DIR}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  file(REMOVE_RECURSE "${build}")
  if(result EQUAL 0)
    set(${out_failed} FALSE PARENT_SCOPE)
  else()
    set(${out_failed} TRUE PARENT_SCOPE)
  endif()
  set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

configure_fixture(NONE failed output)
if(failed)
  string(APPEND report "the scope check rejects a build graph with no crossing:\n${output}\n")
endif()
foreach(edge DIRECT THROUGH_SHARED)
  configure_fixture(${edge} failed output)
  if(NOT failed OR NOT output MATCHES "crosses the client/server split")
    string(APPEND report "the scope check lets a server-only target reach a client-only one (${edge}):\n${output}\n")
  endif()
endforeach()

file(
  GLOB sources
  "${SOURCE_DIR}/src/server/*.h"
  "${SOURCE_DIR}/src/server/*.cpp"
  "${SOURCE_DIR}/src/modules/config/include/augusta/config.h"
  "${SOURCE_DIR}/src/modules/config/include/augusta/server_config.h"
)
if(NOT sources)
  message(FATAL_ERROR "server_boundary.cmake: no server sources found under ${SOURCE_DIR}")
endif()

# Only a whole name counts: `input::` right after a letter, digit or underscore
# (as in `raw_input::`) is some other name. Comments may point at the client's
# side; only code may not reach it.
set(
  forbidden
  "(^|[^A-Za-z0-9_])(input::|ClientConfig|LoadClientConfig|ParseClientConfig)|#include [<\"]augusta/(input|client_config)\\.h"
)

set(violations "")
foreach(source ${sources})
  file(STRINGS "${source}" lines REGEX "${forbidden}")
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    string(STRIP "${line}" line)
    if(line MATCHES "^//")
      continue()
    endif()
    # A semicolon would split the line into list elements.
    string(REPLACE ";" "\\;" line "${line}")
    list(APPEND violations "  ${relative}: ${line}")
  endforeach()
endforeach()
if(violations)
  list(JOIN violations "\n" source_report)
  string(
    APPEND report
    "The server names client Input or the client's config - keep both at the client's edge:\n${source_report}\n"
  )
endif()

if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH sources count)
message(STATUS "server_boundary: ${count} server sources free of client Input; the scope check rejects it")

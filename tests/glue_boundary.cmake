# Executables are Glue (ADR-0060): an executable's own directory holds its
# entry point, its application and its runtime, and nothing else. The Glue
# check (cmake/AugustaModule.cmake) turns that into a configure-time error, and
# this script proves it fires both ways by configuring a fixture build graph
# (tests/glue_fixture) once per shape.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>
# [-DGENERATOR=<generator>] -P glue_boundary.cmake`.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "glue_boundary.cmake: pass -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>")
endif()

set(report "")

# The build's own generator when ctest passes it (-DGENERATOR=...), so the
# fixture needs nothing the build doesn't; CMake's platform default otherwise.
set(generator "")
if(GENERATOR)
  set(generator -G "${GENERATOR}")
endif()

# Configures the fixture as case; sets out_failed and out_output.
function(configure_fixture case out_failed out_output)
  set(build "${BINARY_DIR}/glue_fixture_${case}")
  file(REMOVE_RECURSE "${build}")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -S "${SOURCE_DIR}/tests/glue_fixture" -B "${build}" ${generator} -DCASE=${case}
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

# An executable's own three, and a library outside its directory whose name
# looks like one of its own, configure - with nothing left to except.
configure_fixture(GLUE failed output)
if(failed)
  string(APPEND report "the Glue check rejects an executable's own three Glue targets:\n${output}\n")
elseif(NOT output MATCHES "0 exception")
  string(APPEND report "the Glue check does not report how many exceptions are left:\n${output}\n")
endif()

# A fourth library beside them does not, and the message says which target,
# where it is and which decision it breaks.
configure_fixture(VIOLATION failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets a library target sit inside an executable:\n${output}\n")
elseif(NOT output MATCHES "fixture_exe_mechanism" OR NOT output MATCHES "exe" OR NOT output MATCHES "ADR-0060")
  string(APPEND report "the Glue check's refusal names no target, directory or decision:\n${output}\n")
endif()

# Naming it as an exception lets it through, and the check counts it.
configure_fixture(EXCEPTED failed output)
if(failed)
  string(APPEND report "the Glue check refuses an excepted library target:\n${output}\n")
elseif(NOT output MATCHES "1 exception")
  string(APPEND report "the Glue check does not count the exception it allowed:\n${output}\n")
endif()

# Once it has moved out, the entry naming it is a lie, and the check says so:
# a migration cannot leave the ledger stale.
configure_fixture(STALE failed output)
if(NOT failed)
  string(APPEND report "the Glue check keeps an exception naming a target nothing defines:\n${output}\n")
elseif(NOT output MATCHES "fixture_exe_mechanism")
  string(APPEND report "the Glue check's stale-exception refusal names no entry:\n${output}\n")
endif()

# An executable a build leaves out altogether - augustac, on the Linux jobs
# that configure with AUGUSTA_CLIENT=OFF - never registers its directory, so
# its own entries are neither counted nor read as stale.
configure_fixture(UNBUILT failed output)
if(failed)
  string(APPEND report "the Glue check reads the exceptions of an executable this build leaves out:\n${output}\n")
elseif(NOT output MATCHES "0 exception")
  string(APPEND report "the Glue check counts the exceptions of an executable this build leaves out:\n${output}\n")
endif()

if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
message(STATUS "glue_boundary: the Glue check refuses a library target inside an executable and allows its Glue")

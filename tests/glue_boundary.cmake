# Executables are Glue (ADR-0060): an executable's own directory holds its
# entry point, its application and its runtime, and nothing else, and Glue is
# tested only end to end. The Glue check (cmake/AugustaModule.cmake) turns both
# into a configure-time error, and this script proves it fires both ways by
# configuring a fixture build graph (tests/glue_fixture) once per shape.
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
elseif(NOT output MATCHES "fixture_exe_mechanism \\(exe\\)" OR NOT output MATCHES "ADR-0060")
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

# Glue is tested only end to end, so a test that links an executable's runtime
# without saying so is refused, and the refusal names the test and what it
# links.
configure_fixture(TEST_UNIT failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets an untagged test link an executable's runtime:\n${output}\n")
elseif(NOT output MATCHES "fixture_test" OR NOT output MATCHES "fixture_exe_runtime" OR NOT output MATCHES "ADR-0060")
  string(APPEND report "the Glue check's refusal names no test, Glue target or decision:\n${output}\n")
endif()

# Declaring the test end to end is what makes its own executable's runtime
# allowed, so the application-boundary tests stay possible.
configure_fixture(TEST_END_TO_END failed output)
if(failed)
  string(APPEND report "the Glue check refuses an end-to-end test its own executable's runtime:\n${output}\n")
endif()

# Another executable's Glue is refused whatever kind the test declares, and so
# is the executable itself: an end-to-end test drives its own executable, and
# it drives it through what that executable is built from.
configure_fixture(TEST_FOREIGN failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets an end-to-end test link another executable's runtime:\n${output}\n")
elseif(NOT output MATCHES "fixture_test -> fixture_other_runtime")
  string(APPEND report "the Glue check's cross-executable refusal names no test or Glue target:\n${output}\n")
endif()

configure_fixture(TEST_EXECUTABLE failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets an end-to-end test link the executable itself:\n${output}\n")
elseif(NOT output MATCHES "fixture_test -> fixture_exe:")
  string(APPEND report "the Glue check's refusal of an executable names no test:\n${output}\n")
endif()

# The same ledger excuses a test that links Glue, so a suite that predates the
# rule still configures, and the entry is counted among what is left.
configure_fixture(TEST_EXCEPTED failed output)
if(failed)
  string(APPEND report "the Glue check refuses an excepted test that links an executable's runtime:\n${output}\n")
elseif(NOT output MATCHES "1 exception")
  string(APPEND report "the Glue check does not count the excepted test it allowed:\n${output}\n")
endif()

# Once that test links a module instead, the entry naming it excuses nothing.
configure_fixture(TEST_STALE failed output)
if(NOT failed)
  string(APPEND report "the Glue check keeps an exception naming a test that links no Glue:\n${output}\n")
elseif(NOT output MATCHES "fixture_test \\(fixture_exe\\)")
  string(APPEND report "the Glue check's stale-test refusal names no entry:\n${output}\n")
endif()

if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
message(
  STATUS
  "glue_boundary: the Glue check refuses a library target inside an executable and a test that links Glue it may not"
)

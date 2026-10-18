# Executables are Glue (ADR-0060): an executable's only target is the
# executable, which links only the application, its Runtime, failure, logging
# and version, and no target links an executable. The Glue check
# (cmake/AugustaModule.cmake) turns each into a configure-time error, and this
# script proves it fires both ways by configuring a fixture build graph
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

# An executable linking all it may - the application, every target of its
# Runtime's module, failure, logging, version, the build infrastructure every
# binary links and a system library - and a test linking that Runtime
# configure.
configure_fixture(GLUE failed output)
if(failed)
  string(APPEND report "the Glue check refuses an executable linking only what it may:\n${output}\n")
endif()

# A second target beside the executable is refused, whatever it is called - its
# runtime's name included - and the refusal says which target, where it is and
# which decision it breaks.
configure_fixture(EXTRA_TARGET failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets a second target sit inside an executable's directory:\n${output}\n")
elseif(NOT output MATCHES "fixture_exe_runtime \\(exe\\)" OR NOT output MATCHES "ADR-0060")
  string(APPEND report "the Glue check's refusal names no target, directory or decision:\n${output}\n")
endif()

# An executable linking a module that is not its Runtime's, nor the
# application, failure, logging or version, is refused, and the refusal names
# the link.
configure_fixture(LINK_REFUSED failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets an executable link a module it may not:\n${output}\n")
elseif(NOT output MATCHES "fixture_exe -> fixture_exe_module" OR NOT output MATCHES "ADR-0060")
  string(APPEND report "the Glue check's refusal of a link names no link or decision:\n${output}\n")
endif()

# A test linking the executable itself is refused: an end-to-end test links a
# Runtime, never an executable.
configure_fixture(TEST_EXECUTABLE failed output)
if(NOT failed)
  string(APPEND report "the Glue check lets a test link an executable:\n${output}\n")
elseif(NOT output MATCHES "fixture_test -> fixture_exe")
  string(APPEND report "the Glue check's refusal of a test names no test or executable:\n${output}\n")
endif()

if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
message(
  STATUS
  "glue_boundary: the Glue check refuses a second target inside an executable, a link an executable may not have and a test that links an executable"
)

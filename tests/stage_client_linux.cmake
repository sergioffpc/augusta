# scripts/stage-client.sh's Linux checks (ADR-0058), against a stand-in for
# augustac's build (stage_client_fixture/), linked by the build's own toolchain:
# a folder that bundles what it loads, prints the version asked for and has its
# own debug info passes; a dependency neither bundled nor on the allowlist, a
# wrong version or another build's debug info fails it.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>
# [-DGENERATOR=<generator>] -P stage_client_linux.cmake`.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "stage_client_linux.cmake: pass -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>")
endif()

set(root "${BINARY_DIR}/stage_client_linux")
file(REMOVE_RECURSE "${root}")

set(generator "")
if(GENERATOR)
  set(generator -G "${GENERATOR}")
endif()

# Builds the fixture into ${root}/<name>, bundling its shared library or not.
function(build_fixture name bundled)
  set(build "${root}/${name}")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -S "${SOURCE_DIR}/tests/stage_client_fixture" -B "${build}" ${generator}
      -DCMAKE_TOOLCHAIN_FILE=${SOURCE_DIR}/cmake/LlvmToolchain.cmake -DCMAKE_BUILD_TYPE=Release
      -DAUGUSTA_SOURCE_DIR=${SOURCE_DIR} -DVERSION=1.2.3 -DBUNDLED=${bundled}
    COMMAND_ERROR_IS_FATAL ANY
    OUTPUT_QUIET
  )
  execute_process(COMMAND ${CMAKE_COMMAND} --build "${build}" COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
endfunction()

# Stages a fixture build into a tarball, checked against version; sets
# out_failed and out_output.
function(stage name version out_failed out_output)
  execute_process(
    COMMAND
      bash "${SOURCE_DIR}/scripts/stage-client.sh" "${root}/${name}" "${root}/${name}-staged" "${root}/${name}.tar.gz"
      ${version}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  if(result EQUAL 0)
    set(${out_failed} FALSE PARENT_SCOPE)
  else()
    set(${out_failed} TRUE PARENT_SCOPE)
  endif()
  set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

set(report "")

build_fixture(bundled ON)
stage(bundled 1.2.3 failed output)
if(failed)
  string(APPEND report "a folder that bundles what augustac loads fails the check:\n${output}\n")
else()
  # One augustac/ folder, with the executable and the library it bundles.
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar tf "${root}/bundled.tar.gz"
    OUTPUT_VARIABLE listing
    COMMAND_ERROR_IS_FATAL ANY
  )
  string(REGEX REPLACE "(^|\n)\\./" "\\1" listing "${listing}")
  string(REGEX MATCHALL "[^\n]+" entries "${listing}")
  foreach(entry ${entries})
    if(NOT entry MATCHES "^augustac(/|$)")
      string(APPEND report "the tarball holds ${entry}, outside its augustac/ folder\n")
    endif()
  endforeach()
  foreach(file augustac/augustac augustac/libfixture_bundled.so)
    if(NOT file IN_LIST entries)
      string(APPEND report "the tarball has no ${file}:\n${listing}\n")
    endif()
  endforeach()
endif()

stage(bundled 9.9.9 failed output)
if(NOT failed OR NOT output MATCHES "not 'augustac 9.9.9'")
  string(APPEND report "the check passes an augustac that prints another version:\n${output}\n")
endif()

build_fixture(unbundled OFF)
stage(unbundled "" failed output)
if(NOT failed OR NOT output MATCHES "augustac links libfixture_bundled\\.so")
  string(APPEND report "the check passes a dependency neither bundled nor allowed:\n${output}\n")
endif()

# Another build's debug info, under the name augustac's .gnu_debuglink gives.
file(COPY_FILE "${root}/unbundled/src/client/augustac.debug" "${root}/bundled/src/client/augustac.debug")
stage(bundled 1.2.3 failed output)
if(NOT failed OR NOT output MATCHES "build ID")
  string(APPEND report "the check passes debug info of another build:\n${output}\n")
endif()

file(REMOVE_RECURSE "${root}")
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
message(STATUS "stage_client_linux: the staging check passes a complete folder and fails a broken one")

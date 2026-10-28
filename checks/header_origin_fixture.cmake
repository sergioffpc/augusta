# header_origin.cmake against a stand-in for a build (header_origin_fixture/),
# compiled by the build's own compiler and generator: a build whose headers all
# lie under the allowed roots passes; one with a header under a Visual Studio
# install fails it, and the failure names that header and no other.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>
# -DGENERATOR=<Ninja generator> -DMAKE_PROGRAM=<ninja> -DCOMPILER=<C++ compiler>
# -P header_origin_fixture.cmake`.
foreach(
  input
  SOURCE_DIR
  BINARY_DIR
  GENERATOR
  MAKE_PROGRAM
  COMPILER
)
  if(NOT DEFINED ${input})
    message(
      FATAL_ERROR
      "header_origin_fixture.cmake: pass -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir> "
      "-DGENERATOR=<Ninja generator> -DMAKE_PROGRAM=<ninja> -DCOMPILER=<C++ compiler>"
    )
  endif()
endforeach()

set(root "${BINARY_DIR}/header_origin")
file(REMOVE_RECURSE "${root}")

# The fixture's own directory stands in for the repository: the whole
# repository would hold the scratch directory, and with it the Visual Studio
# stand-in.
set(fixture "${SOURCE_DIR}/checks/header_origin_fixture")
set(sysroot "${root}/xwin/crt/include")
set(visual_studio "${root}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/14.44.35207/include")
file(WRITE "${sysroot}/sysroot_header.h" "#pragma once\ninline int sysroot_header() { return 2; }\n")
file(WRITE "${visual_studio}/visual_studio_header.h" "#pragma once\n")

# Builds the fixture into ${root}/<name>, with the Visual Studio stand-in on its
# include path or not.
function(build_fixture name with_visual_studio)
  set(build "${root}/${name}")
  set(extra "")
  if(with_visual_studio)
    set(extra "-DVISUAL_STUDIO=${visual_studio}")
  endif()
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -S "${fixture}" -B "${build}" -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
      "-DCMAKE_CXX_COMPILER=${COMPILER}" "-DSYSROOT=${sysroot}" ${extra}
    COMMAND_ERROR_IS_FATAL ANY
    OUTPUT_QUIET
  )
  execute_process(COMMAND ${CMAKE_COMMAND} --build "${build}" COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
endfunction()

# Checks the fixture's build in ${root}/<name> against the fixture's directory
# and the sysroot stand-in; sets out_failed and out_output.
function(check name out_failed out_output)
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} "-DBUILD_DIR=${root}/${name}" "-DMAKE_PROGRAM=${MAKE_PROGRAM}" -P
      "${SOURCE_DIR}/checks/header_origin.cmake" "${fixture}" "${sysroot}"
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

build_fixture(allowed OFF)
check(allowed failed output)
if(failed)
  string(APPEND report "a build whose headers all lie under the allowed roots fails the check:\n${output}\n")
endif()

build_fixture(visual_studio ON)
check(visual_studio failed output)
# The output may wrap a long path, so the header's name, not its whole path.
if(NOT failed OR NOT output MATCHES "visual_studio_header\\.h")
  string(APPEND report "the check passes a build with a header under a Visual Studio install:\n${output}\n")
elseif(output MATCHES "sysroot_header\\.h|fixture\\.h")
  string(APPEND report "the check names a header under the allowed roots:\n${output}\n")
endif()

file(REMOVE_RECURSE "${root}")
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
message(STATUS "header_origin_fixture: the check passes a build within its roots and names a header outside them")

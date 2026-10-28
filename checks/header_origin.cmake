# Shows where a build's headers came from (ADR-0079): every file Ninja's deps
# log records for the build, its sources and headers, must lie under one of the
# allowed roots, such as
# the MSVC sysroot, the vcpkg installed tree, the repository and the LLVM
# install. A green Windows build alone cannot show it used the sysroot: clang-cl
# finds a Visual Studio installed beside it on its own. The check fails naming
# each header outside the roots, and fails a deps log that records nothing, so
# it cannot pass by reading nothing.
#
# Run as `cmake -DBUILD_DIR=<Ninja build dir> [-DMAKE_PROGRAM=<ninja>] -P
# header_origin.cmake <allowed root>...`, after the build. Each root is an
# absolute path, and a header lies under it if the root is a prefix of the
# header's path, component by component; on Windows, in any case. Paths are
# compared as written, with no link followed. header_origin_fixture.cmake runs
# it against a stand-in build.
if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "header_origin.cmake: pass -DBUILD_DIR=<Ninja build dir> and the allowed roots")
endif()
if(NOT DEFINED MAKE_PROGRAM)
  set(MAKE_PROGRAM ninja)
endif()

# A path as the check names it: absolute (a relative one, as Ninja records a
# header near the build directory, is the build directory's), with forward
# slashes and no . or ..
function(absolute path out)
  file(TO_CMAKE_PATH "${path}" path)
  cmake_path(ABSOLUTE_PATH path BASE_DIRECTORY "${BUILD_DIR}" NORMALIZE)
  set(${out} "${path}" PARENT_SCOPE)
endfunction()

# A path as the check compares it: absolute and, on Windows, in lower case.
function(comparable path out)
  absolute("${path}" path)
  if(CMAKE_HOST_WIN32)
    string(TOLOWER "${path}" path)
  endif()
  set(${out} "${path}" PARENT_SCOPE)
endfunction()

# The roots: the arguments after the script's own name.
set(roots "")
set(after_script FALSE)
math(EXPR last "${CMAKE_ARGC} - 1")
foreach(index RANGE ${last})
  set(argument "${CMAKE_ARGV${index}}")
  if(after_script)
    file(TO_CMAKE_PATH "${argument}" root)
    cmake_path(IS_ABSOLUTE root absolute)
    if(NOT absolute)
      message(FATAL_ERROR "header_origin.cmake: the allowed root '${argument}' is not an absolute path")
    endif()
    comparable("${root}" root)
    list(APPEND roots "${root}")
  elseif(argument MATCHES "header_origin\\.cmake$")
    set(after_script TRUE)
  endif()
endforeach()
if(NOT roots)
  message(FATAL_ERROR "header_origin.cmake: pass at least one allowed root after the script")
endif()

# `ninja -t deps` lists each output, then its dependencies indented beneath it.
execute_process(
  COMMAND "${MAKE_PROGRAM}" -C "${BUILD_DIR}" -t deps
  OUTPUT_VARIABLE log
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "header_origin.cmake: cannot read the deps log of ${BUILD_DIR}:\n${error}")
endif()
string(REPLACE "\n" ";" lines "${log}")
list(FILTER lines INCLUDE REGEX "^[ \t]+[^ \t]")
list(TRANSFORM lines STRIP)
list(REMOVE_DUPLICATES lines)
if(NOT lines)
  message(FATAL_ERROR "header_origin.cmake: the deps log of ${BUILD_DIR} records no header: was it built, with Ninja?")
endif()

set(outside "")
foreach(header ${lines})
  comparable("${header}" path)
  set(allowed FALSE)
  foreach(root ${roots})
    cmake_path(IS_PREFIX root "${path}" allowed)
    if(allowed)
      break()
    endif()
  endforeach()
  if(NOT allowed)
    absolute("${header}" shown)
    string(APPEND outside "\n  ${shown}")
  endif()
endforeach()

if(outside)
  string(REPLACE ";" "\n  " shown "${roots}")
  message(
    FATAL_ERROR
    "headers from outside the allowed roots, so the build did not take them from where it should:${outside}\n"
    "The allowed roots:\n  ${shown}"
  )
endif()

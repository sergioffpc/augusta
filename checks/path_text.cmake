# Keeps path::string() and path::generic_string() out of src/
# (docs/agents/coding-standards.md): on Windows both convert a path to the ANSI
# code page and throw for a character it lacks, so a log line, a failure's
# context or a message naming a path in a folder it lacks would stop the
# process. logging::Utf8 is a path's text; a path made from another is made
# with path operations, never through a string.
#
# Run as `cmake -DSOURCE_DIR=<repo root> -P path_text.cmake [file...]`, as
# `make format-check/path-text` runs it: the .cpp and .h files given, relative
# to SOURCE_DIR, or every tracked one under src/ without any, failing if git
# lists none. The scan proves itself first on a planted file, so it cannot pass
# by matching nothing.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "path_text.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

# A call of either, on an object or through a pointer: u8string() and
# generic_u8string() are other names.
set(forbidden "(\\.|->)(generic_)?string\\(\\)")

# Appends to the caller's violations each line of path, named as where, that
# calls either.
function(scan path where)
  file(STRINGS "${path}" lines REGEX "${forbidden}")
  set(found "${violations}")
  foreach(line ${lines})
    if(line MATCHES "${forbidden}")
      string(STRIP "${line}" line)
      string(APPEND found "\n  ${where}: ${line}")
    endif()
  endforeach()
  set(violations "${found}" PARENT_SCOPE)
endfunction()

# First a planted file, in the system's temporary directory: the scan must
# refuse its two calls and pass its two UTF-8 ones.
if(DEFINED ENV{TEMP})
  set(temporary "$ENV{TEMP}")
elseif(DEFINED ENV{TMPDIR})
  set(temporary "$ENV{TMPDIR}")
else()
  set(temporary "/tmp")
endif()
string(RANDOM LENGTH 12 suffix)
set(planted "${temporary}/augusta_path_text_${suffix}.cpp")
file(
  WRITE "${planted}"
  "auto a = path.string();\nauto b = entry->path().generic_string();\n"
  "auto c = path.u8string();\nauto d = path.generic_u8string();\n"
)
set(violations "")
scan("${planted}" "planted")
file(REMOVE "${planted}")
string(REGEX MATCHALL "\n  planted: " planted_lines "${violations}")
list(LENGTH planted_lines planted_count)
if(NOT planted_count EQUAL 2)
  message(FATAL_ERROR "path_text.cmake: the scan finds ${planted_count} of the 2 planted calls:${violations}")
endif()

# The files on the command line, after the script's own name.
set(files "")
math(EXPR last "${CMAKE_ARGC} - 1")
foreach(index RANGE ${last})
  if(CMAKE_ARGV${index} MATCHES "\\.(cpp|h)$")
    list(APPEND files "${CMAKE_ARGV${index}}")
  endif()
endforeach()
if(NOT files)
  execute_process(
    COMMAND git ls-files -- "src/*.cpp" "src/*.h"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE listed
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  string(REPLACE "\n" ";" files "${listed}")
  if(NOT files)
    message(FATAL_ERROR "path_text.cmake: git lists no source under ${SOURCE_DIR}/src")
  endif()
endif()

set(violations "")
foreach(file ${files})
  scan("${SOURCE_DIR}/${file}" "${file}")
endforeach()
if(violations)
  message(
    FATAL_ERROR
    "path::string() or generic_string() in src/, which on Windows throws for a character outside the ANSI "
    "code page: use logging::Utf8 for a path's text, and path operations for a path made from another.${violations}"
  )
endif()

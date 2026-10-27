# Test areas (ADR-0008): each test executable's gtest_discover_tests call names
# the one functional area it tests among its ctest labels, e.g.
#   gtest_discover_tests(augusta_pose_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS animation)
#   gtest_discover_tests(augusta_x_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "protocol;property")
# The calls' own text is what declares the areas: tests/CMakeLists.txt reads it
# to make each area's build target, and a `cmake -P` source scan can read it
# the same way, with no build. Included by both, so it reads only files.

# The names an area may take: a runtime module's (src/modules/<module>),
# client and server for their Runtimes' tests, and a tool's (tools/<tool>).
# Every other label (check, property, netcode) is not an area.
function(augusta_test_area_vocabulary source_dir out_vocabulary)
  file(GLOB modules LIST_DIRECTORIES true RELATIVE "${source_dir}/src/modules" "${source_dir}/src/modules/*")
  file(GLOB tools LIST_DIRECTORIES true RELATIVE "${source_dir}/tools" "${source_dir}/tools/*")
  set(vocabulary client server)
  foreach(name IN LISTS modules tools)
    if(IS_DIRECTORY "${source_dir}/src/modules/${name}" OR IS_DIRECTORY "${source_dir}/tools/${name}")
      list(APPEND vocabulary ${name})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES vocabulary)
  set(${out_vocabulary} ${vocabulary} PARENT_SCOPE)
endfunction()

# The gtest_discover_tests calls of a listfile, comments left out: sets
# <prefix>_TARGETS to the executables they discover, in order, and
# <prefix>_LABELS_<target> to the ctest labels each call gives (empty for
# none), and <prefix>_LINE_<target> to the line the call starts on.
function(augusta_read_test_labels listfile prefix)
  file(READ "${listfile}" text)
  # One line per list element, so a call's line is its element's index.
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")
  string(REPLACE ";" "\\;" text "${text}")
  string(REPLACE "\n" ";" lines "${text}")
  set(targets "")
  set(call "")
  set(line_number 0)
  foreach(line IN LISTS lines)
    math(EXPR line_number "${line_number} + 1")
    if(call STREQUAL "")
      string(FIND "${line}" "gtest_discover_tests(" start)
      if(start EQUAL -1)
        continue()
      endif()
      string(SUBSTRING "${line}" ${start} -1 call)
      set(call_line ${line_number})
    else()
      string(APPEND call " ${line}")
    endif()
    string(FIND "${call}" ")" end)
    if(end EQUAL -1)
      continue()
    endif()
    string(REGEX REPLACE "[ \t\r]+" " " call "${call}")
    string(REGEX MATCH "^gtest_discover_tests\\( ?([A-Za-z0-9_]+)" target_match "${call}")
    set(target "${CMAKE_MATCH_1}")
    set(labels "")
    if(call MATCHES " PROPERTIES .*LABELS (\"[^\"]*\"|[^ )]+)")
      string(REPLACE "\"" "" labels "${CMAKE_MATCH_1}")
      string(REPLACE "\\;" ";" labels "${labels}")
    endif()
    list(APPEND targets ${target})
    set(${prefix}_LABELS_${target} "${labels}" PARENT_SCOPE)
    set(${prefix}_LINE_${target} ${call_line} PARENT_SCOPE)
    set(call "")
  endforeach()
  set(${prefix}_TARGETS ${targets} PARENT_SCOPE)
endfunction()

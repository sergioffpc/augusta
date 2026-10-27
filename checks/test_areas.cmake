# Every test executable carries exactly one area (ADR-0008, ADR-0013): each
# gtest_discover_tests call in tests/, tests/fuzz/ and a tool's tests/ names
# one area among its ctest labels, beside check, property and netcode, so
# `make test TAGS=<area>` builds and runs it, and no executable falls out of
# every area's run. The check refuses a call with no area, one with two, and
# one whose area is no module's, Runtime's or tool's (cmake/TestAreas.cmake),
# each naming the file and line. It proves itself first on a planted violation
# of each, so it cannot pass by matching nothing. A source scan, so it builds
# nothing.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>
# -P test_areas.cmake`.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "test_areas.cmake: pass -DSOURCE_DIR=<repo root> -DBINARY_DIR=<scratch dir>")
endif()

include(${SOURCE_DIR}/cmake/TestAreas.cmake)
augusta_test_area_vocabulary(${SOURCE_DIR} vocabulary)

# The labels a call may carry beside its area: make check's, the nightly's
# property run's and the netcode presets' (ADR-0013).
set(other_labels check property netcode)

# What of listfile's gtest_discover_tests calls has no area, two or an unknown
# one: each a line of out_violations, `<relative>:<line>: <what>`, with the
# number of calls read in out_count.
function(check_areas listfile relative out_violations out_count)
  augusta_read_test_labels("${listfile}" discovered)
  set(violations "")
  foreach(target IN LISTS discovered_TARGETS)
    set(where "${relative}:${discovered_LINE_${target}}: ${target}")
    set(areas "")
    set(unknown "")
    foreach(label IN LISTS discovered_LABELS_${target})
      if(label IN_LIST vocabulary)
        list(APPEND areas ${label})
      elseif(NOT label IN_LIST other_labels)
        list(APPEND unknown ${label})
      endif()
    endforeach()
    list(LENGTH areas area_count)
    if(unknown)
      list(JOIN unknown ", " unknown)
      list(APPEND violations "${where} names ${unknown}, no area: a module's, client, server or a tool's")
    elseif(area_count EQUAL 0)
      list(APPEND violations "${where} names no area among its labels")
    elseif(area_count GREATER 1)
      list(JOIN areas ", " areas)
      list(APPEND violations "${where} names two areas, ${areas}: an executable tests one")
    endif()
  endforeach()
  list(LENGTH discovered_TARGETS count)
  set(${out_violations} "${violations}" PARENT_SCOPE)
  set(${out_count} ${count} PARENT_SCOPE)
endfunction()

# First a planted listfile with one call of each violation, which the check
# must refuse at its line, and calls it must let through: an area alone, one
# beside the other labels, and one over two lines.
set(planted "${BINARY_DIR}/test_areas_planted.cmake")
file(
  WRITE "${planted}"
  "gtest_discover_tests(augusta_none_tests DISCOVERY_TIMEOUT 30)\n"
  "gtest_discover_tests(augusta_two_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS \"animation;audio\")\n"
  "# A comment naming gtest_discover_tests(augusta_comment_tests) is no call.\n"
  "gtest_discover_tests(augusta_unknown_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS \"animations;property\")\n"
  "gtest_discover_tests(augusta_one_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS animation)\n"
  "gtest_discover_tests(augusta_beside_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS \"protocol;property\")\n"
  "gtest_discover_tests(\n  augusta_split_tests\n  DISCOVERY_TIMEOUT 30\n  PROPERTIES LABELS \"agent;netcode\" TIMEOUT 900\n)\n"
)
check_areas("${planted}" "planted" planted_violations planted_count)
file(REMOVE "${planted}")
set(self_test "")
set(
  expected
  "planted:1: augusta_none_tests names no area"
  "planted:2: augusta_two_tests names two areas, animation, audio"
  "planted:4: augusta_unknown_tests names animations, no area"
)
foreach(refusal IN LISTS expected)
  string(FIND "${planted_violations}" "${refusal}" found)
  if(found EQUAL -1)
    string(APPEND self_test "  it does not refuse the planted \"${refusal}\"\n")
  endif()
endforeach()
if(NOT planted_count EQUAL 6)
  string(APPEND self_test "  it reads ${planted_count} planted calls, not 6\n")
endif()
list(LENGTH planted_violations planted_violation_count)
if(NOT planted_violation_count EQUAL 3)
  string(APPEND self_test "  it refuses ${planted_violation_count} planted calls, not 3:\n")
  foreach(violation IN LISTS planted_violations)
    string(APPEND self_test "    ${violation}\n")
  endforeach()
endif()
if(self_test)
  message(FATAL_ERROR "test_areas.cmake: the check fails its planted violations:\n${self_test}")
endif()

# Then the tree's test directories, as tests/CMakeLists.txt lists them.
file(GLOB tool_tests RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/tools/*/tests/CMakeLists.txt")
set(violations "")
set(total 0)
foreach(relative tests/CMakeLists.txt tests/fuzz/CMakeLists.txt ${tool_tests})
  check_areas("${SOURCE_DIR}/${relative}" "${relative}" listfile_violations count)
  if(count EQUAL 0)
    message(FATAL_ERROR "test_areas.cmake: no gtest_discover_tests call found in ${relative}")
  endif()
  math(EXPR total "${total} + ${count}")
  list(APPEND violations ${listfile_violations})
endforeach()
if(violations)
  list(JOIN violations "\n" violations)
  message(FATAL_ERROR "Every test executable names exactly one area (ADR-0008):\n${violations}")
endif()
message(STATUS "test_areas: each of ${total} test executables names exactly one area")

# Keeps the shared core's base below the network layer and the client's device
# input: no public header of physics or of the simulation module
# (SimulationWorld and Replication) may name a protocol type or the input
# sampler's module, or include either's header. The grids a body lives on are
# augusta::math's (augusta/grid.h) and the Command SimulationWorld consumes is
# augusta::command's, so neither needs them.
#
# Keeps tick (augusta/tick.h) and limits (augusta/limits.h) below everything:
# the Tick and command Sequence, the tick schedule and the player, recoil and
# Shot judgement bounds the engine and the protocol share may include no augusta header but
# their own, name no other augusta module, nor link any target, so the protocol
# can take them without taking the gameplay modules that use them. And nothing
# else gives the Tick or the Sequence a type of its own, or declares a bound
# again (the commands-per-message bound is the protocol's own), with a value of
# its own or as an alias.
#
# Keeps the harness and the host on the Link alone (ADR-0038): no source of
# either may include the protocol's, networking's or the Link's conversions'
# header or name the protocol or networking, and neither's CMakeLists.txt may
# link any of their targets, so every message each exchanges goes through
# augusta/link.h, the replay server's among them (ADR-0051).
#
# Keeps capture on the Link's conversions alone (ADR-0038): its CMakeLists.txt
# links augusta_link_messages and never augusta_link or augusta_protocol, and
# no source of it names the protocol or includes its header, so each record's
# bytes are the Link's to make. The check proves itself first on a planted
# violation.
#
# Keeps networking beneath the Link alone (ADR-0038): no target but the Link's
# own (augusta_link, and augusta_link_raw, its raw ends for tests) links
# augusta_networking, bar networking's own tests and the Link's test of what it
# decides about the transport (augusta_link_transport_tests), which reach the
# private headers each tests, and the pcapng writer (augusta_pcapng, ADR-0076),
# a Development-only tap that takes networking's tap types and sends nothing.
# Every other end of the transport, a test's raw one too, is the Link's
# (augusta/link.h, raw_link.h). The check proves itself first on a planted
# violation, so it cannot pass by matching nothing.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo root> -P core_boundary.cmake`.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "core_boundary.cmake: pass -DSOURCE_DIR=<repo root>")
endif()

file(
  GLOB headers
  "${SOURCE_DIR}/src/modules/physics/include/augusta/*.h"
  "${SOURCE_DIR}/src/modules/simulation/include/augusta/*.h"
)
if(NOT headers)
  message(FATAL_ERROR "core_boundary.cmake: no headers found under ${SOURCE_DIR}")
endif()

# Only a whole name counts: `input::` right after a letter, digit or underscore
# (as in `raw_input::`) is some other name.
set(forbidden "(^|[^A-Za-z0-9_])(protocol|input)::|augusta/(protocol|input)\\.h")

set(violations "")
foreach(header ${headers})
  file(STRINGS "${header}" lines REGEX "${forbidden}")
  foreach(line ${lines})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    string(STRIP "${line}" line)
    list(APPEND violations "  ${relative}: ${line}")
  endforeach()
endforeach()

# Any augusta include, or any augusta:: name in code but the module's own.
set(any_augusta "#[ \t]*include[ \t]*[\"<]augusta/|augusta::[A-Za-z_]")
set(base_violations "")
set(base_count 0)
foreach(module tick limits)
  file(
    GLOB module_sources
    "${SOURCE_DIR}/src/modules/${module}/include/augusta/*.h"
    "${SOURCE_DIR}/src/modules/${module}/*.cpp"
  )
  if(NOT module_sources)
    message(FATAL_ERROR "core_boundary.cmake: no ${module} sources found under ${SOURCE_DIR}")
  endif()
  list(LENGTH module_sources module_count)
  math(EXPR base_count "${base_count} + ${module_count}")
  foreach(source ${module_sources})
    file(STRINGS "${source}" lines REGEX "${any_augusta}")
    foreach(line ${lines})
      string(STRIP "${line}" line)
      # A comment may name what the module stands alongside; only code depends on it.
      if(
        line MATCHES "^//"
        OR line MATCHES "namespace augusta::${module}([^A-Za-z0-9_]|$)"
        OR line MATCHES "#[ \t]*include[ \t]*\"augusta/${module}\.h\""
      )
        continue()
      endif()
      file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
      list(APPEND base_violations "  ${relative}: ${line}")
    endforeach()
  endforeach()
  file(STRINGS "${SOURCE_DIR}/src/modules/${module}/CMakeLists.txt" lines REGEX "target_link_libraries")
  foreach(line ${lines})
    string(STRIP "${line}" line)
    list(APPEND base_violations "  src/modules/${module}/CMakeLists.txt: ${line}")
  endforeach()
endforeach()

# Nothing else gives a Tick or a Sequence a type of its own, even as an alias
# of tick's, or declares a player, recoil or Shot judgement bound again
# (limits' own) or the commands-per-message bound (the protocol's own), with a
# number, as an alias or by a using-declaration: so the engine and the wire cannot drift apart and no
# module re-exports them as its own. A line scan, so it catches the plain
# spellings, not every one C++ allows.
file(
  GLOB_RECURSE sources
  "${SOURCE_DIR}/src/*.h"
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/tools/*.h"
  "${SOURCE_DIR}/tools/*.cpp"
)
list(FILTER sources EXCLUDE REGEX "/src/modules/(tick|limits)/")
set(
  own_definition
  "using[ \t]+(Tick|Sequence)[ \t]*=|k(MaxPlayers|MaxCommandsPerMessage|MaxRecoilKicks|MaxShotJudgementEntries)[ \t]*(=([^=]|$)|{)|using[ \t]+[A-Za-z_:]*::k(MaxPlayers|MaxCommandsPerMessage|MaxRecoilKicks|MaxShotJudgementEntries)"
)
set(protocol_header "/src/modules/protocol/include/augusta/protocol[.]h$")
set(protocol_own "^inline constexpr std::size_t kMaxCommandsPerMessage = [0-9]+")
set(definition_violations "")
foreach(source ${sources})
  file(STRINGS "${source}" lines REGEX "${own_definition}")
  foreach(line ${lines})
    # A definition's closing semicolon splits the line as a CMake list.
    if(NOT line MATCHES "${own_definition}")
      continue()
    endif()
    string(STRIP "${line}" line)
    if(source MATCHES "${protocol_header}" AND line MATCHES "${protocol_own}")
      continue()
    endif()
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    list(APPEND definition_violations "  ${relative}: ${line}")
  endforeach()
endforeach()

# What of module's sources, and of the targets its CMakeLists.txt links, reaches
# beneath the Link: each a line of out_violations, with the number of sources
# scanned in out_count.
set(beneath_the_link "(^|[^A-Za-z0-9_])(protocol|networking)::|augusta/(protocol|networking|link_messages)\\.h")
set(beneath_the_link_targets "(^|[^A-Za-z0-9_])augusta_(protocol|networking|link_messages)([^A-Za-z0-9_]|$)")
function(check_on_the_link module out_violations out_count)
  file(
    GLOB_RECURSE module_sources
    "${SOURCE_DIR}/src/modules/${module}/*.h"
    "${SOURCE_DIR}/src/modules/${module}/*.cpp"
  )
  if(NOT module_sources)
    message(FATAL_ERROR "core_boundary.cmake: no ${module} sources found under ${SOURCE_DIR}")
  endif()
  list(LENGTH module_sources module_count)
  set(violations "")
  foreach(source ${module_sources})
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    file(STRINGS "${source}" lines REGEX "${beneath_the_link}")
    foreach(line ${lines})
      string(STRIP "${line}" line)
      # A comment may name what lies beneath the Link; only code depends on it.
      if(line MATCHES "^//")
        continue()
      endif()
      list(APPEND violations "  ${relative}: ${line}")
    endforeach()
  endforeach()
  # Each target_link_libraries call whole, comments and all lines of it, so
  # the target it links for is known.
  file(READ "${SOURCE_DIR}/src/modules/${module}/CMakeLists.txt" listfile)
  string(REGEX REPLACE "#[^\n]*" "" listfile "${listfile}")
  string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" calls "${listfile}")
  foreach(call ${calls})
    string(REGEX REPLACE "[ \t\r\n]+" " " call "${call}")
    if(call MATCHES "${beneath_the_link_targets}")
      list(APPEND violations "  src/modules/${module}/CMakeLists.txt: ${call}")
    endif()
  endforeach()
  set(${out_violations} "${violations}" PARENT_SCOPE)
  set(${out_count} "${module_count}" PARENT_SCOPE)
endfunction()

check_on_the_link(harness harness_violations harness_count)
check_on_the_link(host host_violations host_count)

# What of listfile, capture's CMakeLists.txt text, links past the Link's
# conversions: each target_link_libraries call that links augusta_link or
# augusta_protocol, or a line saying none links augusta_link_messages, in
# out_violations, the calls named by where.
set(past_the_conversions "(^|[^A-Za-z0-9_])augusta_(link|protocol)([^A-Za-z0-9_]|$)")
set(the_conversions "(^|[^A-Za-z0-9_])augusta_link_messages([^A-Za-z0-9_]|$)")
function(check_capture_links listfile where out_violations)
  string(REGEX REPLACE "#[^\n]*" "" listfile "${listfile}")
  string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" calls "${listfile}")
  set(violations "")
  set(converts FALSE)
  foreach(call ${calls})
    string(REGEX REPLACE "[ \t\r\n]+" " " call "${call}")
    if(call MATCHES "${past_the_conversions}")
      list(APPEND violations "  ${where}: ${call}")
    endif()
    if(call MATCHES "${the_conversions}")
      set(converts TRUE)
    endif()
  endforeach()
  if(NOT converts)
    list(APPEND violations "  ${where}: links no augusta_link_messages")
  endif()
  set(${out_violations} "${violations}" PARENT_SCOPE)
endfunction()

# First a planted capture that links the protocol and the Link, which the
# check must refuse, and one linking the conversions alone, which it must let
# through: so it cannot pass by matching nothing.
set(capture_self_test "")
check_capture_links(
  "target_link_libraries(\n  augusta_capture\n  # a comment (with parentheses)\n  PRIVATE augusta_link_messages augusta_protocol\n)\ntarget_link_libraries(augusta_capture PRIVATE augusta_link)"
  "planted"
  planted_capture_violations
)
list(LENGTH planted_capture_violations planted_capture_count)
if(NOT planted_capture_count EQUAL 2)
  string(APPEND capture_self_test "  it lets a planted capture link augusta_protocol or augusta_link\n")
endif()
check_capture_links(
  "target_link_libraries(augusta_capture PUBLIC augusta_failure PRIVATE augusta_link_messages)"
  "allowed"
  allowed_capture_violations
)
if(allowed_capture_violations)
  string(APPEND capture_self_test "  it refuses what it allows:\n${allowed_capture_violations}\n")
endif()

file(READ "${SOURCE_DIR}/src/modules/capture/CMakeLists.txt" capture_listfile)
check_capture_links("${capture_listfile}" "src/modules/capture/CMakeLists.txt" capture_violations)
file(GLOB_RECURSE capture_sources "${SOURCE_DIR}/src/modules/capture/*.h" "${SOURCE_DIR}/src/modules/capture/*.cpp")
if(NOT capture_sources)
  message(FATAL_ERROR "core_boundary.cmake: no capture sources found under ${SOURCE_DIR}")
endif()
list(LENGTH capture_sources capture_count)
foreach(source ${capture_sources})
  file(STRINGS "${source}" lines REGEX "(^|[^A-Za-z0-9_])protocol::|augusta/protocol\\.h")
  foreach(line ${lines})
    string(STRIP "${line}" line)
    # A comment may name the protocol; only code depends on it.
    if(NOT line MATCHES "^//")
      file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
      list(APPEND capture_violations "  ${relative}: ${line}")
    endif()
  endforeach()
endforeach()

# Each target_link_libraries call of listfile, a CMake listfile's text, that
# links augusta_networking for a target allowed none: each a line of
# out_violations, the call named by where.
set(networking_target "(^|[^A-Za-z0-9_])augusta_networking([^A-Za-z0-9_]|$)")
set(
  networking_linkers
  augusta_link
  augusta_link_raw
  augusta_networking_tests
  augusta_send_flags_tests
  augusta_transport_events_tests
  augusta_link_transport_tests
  augusta_pcapng
)
function(check_networking_links listfile where out_violations)
  string(REGEX REPLACE "#[^\n]*" "" listfile "${listfile}")
  string(REGEX MATCHALL "target_link_libraries\\([^)]*\\)" calls "${listfile}")
  set(violations "")
  foreach(call ${calls})
    string(REGEX REPLACE "[ \t\r\n]+" " " call "${call}")
    string(REGEX MATCH "^target_link_libraries\\( ?([A-Za-z0-9_]+)(.*)$" target_match "${call}")
    set(target "${CMAKE_MATCH_1}")
    set(linked "${CMAKE_MATCH_2}")
    if(linked MATCHES "${networking_target}" AND NOT target IN_LIST networking_linkers)
      list(APPEND violations "  ${where}: ${call}")
    endif()
  endforeach()
  set(${out_violations} "${violations}" PARENT_SCOPE)
endfunction()

# First a planted target that links networking, which the check must refuse,
# and the Link's own targets and networking itself, which it must let through:
# so it cannot pass by matching nothing.
set(networking_self_test "")
check_networking_links(
  "target_link_libraries(\n  augusta_planted\n  # a comment (with parentheses)\n  PRIVATE augusta_logging augusta_networking\n)"
  "planted"
  planted_violations
)
if(NOT planted_violations MATCHES "augusta_planted")
  string(APPEND networking_self_test "  it lets a planted target link augusta_networking\n")
endif()
check_networking_links(
  "target_link_libraries(augusta_link PRIVATE augusta_networking)\ntarget_link_libraries(augusta_link_raw PRIVATE augusta_networking)\ntarget_link_libraries(augusta_networking PUBLIC augusta_failure)"
  "allowed"
  allowed_violations
)
if(allowed_violations)
  string(APPEND networking_self_test "  it refuses what it allows:\n${allowed_violations}\n")
endif()

file(
  GLOB_RECURSE listfiles
  "${SOURCE_DIR}/src/*CMakeLists.txt"
  "${SOURCE_DIR}/tests/*CMakeLists.txt"
  "${SOURCE_DIR}/checks/*CMakeLists.txt"
  "${SOURCE_DIR}/tools/*CMakeLists.txt"
  "${SOURCE_DIR}/cmake/*.cmake"
)
list(APPEND listfiles "${SOURCE_DIR}/CMakeLists.txt")
list(FILTER listfiles EXCLUDE REGEX "/(\\.venv|node_modules)/")
list(LENGTH listfiles listfile_count)
set(networking_violations "")
foreach(path ${listfiles})
  file(READ "${path}" listfile)
  file(RELATIVE_PATH relative "${SOURCE_DIR}" "${path}")
  check_networking_links("${listfile}" "${relative}" found)
  list(APPEND networking_violations ${found})
endforeach()

set(report "")
if(violations)
  list(JOIN violations "\n" core_report)
  string(
    APPEND report
    "physics or the simulation module names the protocol or the input sampler - use augusta/grid.h or augusta::command instead:\n${core_report}\n"
  )
endif()
if(base_violations)
  list(JOIN base_violations "\n" base_report)
  string(
    APPEND report
    "tick or limits depends on another module - they must stay below the engine and the protocol alike:\n${base_report}\n"
  )
endif()
if(definition_violations)
  list(JOIN definition_violations "\n" definition_report)
  string(
    APPEND report
    "A counter or bound tick, limits or the protocol owns is defined again - take it from augusta/tick.h, augusta/limits.h or augusta/protocol.h instead:\n${definition_report}\n"
  )
endif()
if(harness_violations)
  list(JOIN harness_violations "\n" harness_report)
  string(
    APPEND report
    "The harness reaches beneath the Link - send and receive through augusta/link.h instead:\n${harness_report}\n"
  )
endif()
if(host_violations)
  list(JOIN host_violations "\n" host_report)
  string(
    APPEND report
    "The host reaches beneath the Link - send and receive through augusta/link.h instead:\n${host_report}\n"
  )
endif()
if(capture_self_test)
  string(APPEND report "The capture check does not fire as it must:\n${capture_self_test}")
endif()
if(capture_violations)
  list(JOIN capture_violations "\n" capture_report)
  string(
    APPEND report
    "capture reaches past the Link's conversions - link augusta_link_messages alone and convert through augusta/link_messages.h:\n${capture_report}\n"
  )
endif()
if(networking_self_test)
  string(APPEND report "The networking check does not fire as it must:\n${networking_self_test}")
endif()
if(networking_violations)
  list(JOIN networking_violations "\n" networking_report)
  string(
    APPEND report
    "A target other than the Link's own links augusta_networking - speak through augusta/link.h (in a test, raw_link.h) instead:\n${networking_report}\n"
  )
endif()
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH headers count)
message(
  STATUS
  "core_boundary: ${count} headers free of protocol and input types, ${base_count} tick and limits sources free of other modules and defined nowhere else, ${harness_count} harness and ${host_count} host sources on the Link alone, ${capture_count} capture sources on the Link's conversions alone, and ${listfile_count} listfiles linking networking only for the Link's own targets and the transport's own tests"
)

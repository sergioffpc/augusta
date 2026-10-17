# Keeps the shared core's base below the network layer and the client's device
# input: no public header of physics or of the simulation module
# (SimulationWorld and Replication) may name a protocol type or the input
# sampler's module, or include either's header. The grids a body lives on are
# augusta::math's (augusta/grid.h) and the Command SimulationWorld consumes is
# augusta::command's, so neither needs them.
#
# Keeps tick (augusta/tick.h) and limits (augusta/limits.h) below everything:
# the Tick and command Sequence, the tick schedule and the player and recoil
# bounds the engine and the protocol share may include no augusta header but
# their own, name no other augusta module, nor link any target, so the protocol
# can take them without taking the gameplay modules that use them. And nothing
# else gives the Tick or the Sequence a type of its own, or declares a bound
# again (the commands-per-message bound is the protocol's own), with a value of
# its own or as an alias.
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
# of tick's, or declares a player or recoil bound again (limits' own) or the
# commands-per-message bound (the protocol's own), with a number, as an alias or
# by a using-declaration: so the engine and the wire cannot drift apart and no
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
  "using[ \t]+(Tick|Sequence)[ \t]*=|k(MaxPlayers|MaxCommandsPerMessage|MaxRecoilKicks)[ \t]*(=([^=]|$)|{)|using[ \t]+[A-Za-z_:]*::k(MaxPlayers|MaxCommandsPerMessage|MaxRecoilKicks)"
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
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH headers count)
message(
  STATUS
  "core_boundary: ${count} headers free of protocol and input types, ${base_count} tick and limits sources free of other modules and defined nowhere else"
)

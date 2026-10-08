# Keeps the shared core's base below the network layer and the client's device
# input: no public header of physics or SimulationWorld may name a protocol type
# or the input sampler's module, or include either's header. The grids a body
# lives on are augusta::math's (augusta/grid.h) and the Command SimulationWorld consumes is
# augusta::command's, so neither needs them.
#
# Keeps the neutral primitives (augusta/primitives.h) below everything: the
# Tick and command sequence widths and the player, command and recoil bounds
# the engine and the protocol share may include no augusta header, name no
# other augusta module, nor link any target, so the protocol can take them
# without taking the gameplay modules that use them. And nothing else defines
# them again with a value of its own.
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

file(GLOB primitives_headers "${SOURCE_DIR}/src/modules/primitives/include/augusta/*.h")
if(NOT primitives_headers)
  message(FATAL_ERROR "core_boundary.cmake: no primitives headers found under ${SOURCE_DIR}")
endif()

# Any augusta include, or any augusta:: name in code but its own namespace.
set(any_augusta "#[ \t]*include[ \t]*[\"<]augusta/|augusta::[A-Za-z_]")
set(primitives_violations "")
foreach(header ${primitives_headers})
  file(STRINGS "${header}" lines REGEX "${any_augusta}")
  foreach(line ${lines})
    string(STRIP "${line}" line)
    # A comment may name what the primitives stand alongside; only code depends on it.
    if(line MATCHES "^//" OR line MATCHES "namespace augusta::primitives")
      continue()
    endif()
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${header}")
    list(APPEND primitives_violations "  ${relative}: ${line}")
  endforeach()
endforeach()
set(primitives_cmake "${SOURCE_DIR}/src/modules/primitives/CMakeLists.txt")
file(STRINGS "${primitives_cmake}" lines REGEX "target_link_libraries")
foreach(line ${lines})
  string(STRIP "${line}" line)
  list(APPEND primitives_violations "  src/modules/primitives/CMakeLists.txt: ${line}")
endforeach()

# Nothing else gives a Tick or Sequence an integer type of its own, or a
# player, command or recoil bound a number of its own: they take the
# primitives', so the engine and the wire cannot drift apart. A line scan, so
# it catches the plain spellings, not every one C++ allows.
file(
  GLOB_RECURSE sources
  "${SOURCE_DIR}/src/*.h"
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/tools/*.h"
  "${SOURCE_DIR}/tools/*.cpp"
)
list(FILTER sources EXCLUDE REGEX "/src/modules/primitives/")
set(
  own_definition
  "using[ \t]+(Tick|Sequence)[ \t]*=[ \t]*(std::)?(u?int|unsigned|size_t)|k(MaxPlayers|MaxCommandsPerMessage|MaxRecoilKicks)[ \t]*(=[ \t]*|{)[0-9]"
)
set(definition_violations "")
foreach(source ${sources})
  file(STRINGS "${source}" lines REGEX "${own_definition}")
  foreach(line ${lines})
    # A definition's closing semicolon splits the line as a CMake list.
    if(NOT line MATCHES "${own_definition}")
      continue()
    endif()
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${source}")
    string(STRIP "${line}" line)
    list(APPEND definition_violations "  ${relative}: ${line}")
  endforeach()
endforeach()

set(report "")
if(violations)
  list(JOIN violations "\n" core_report)
  string(
    APPEND report
    "physics or SimulationWorld names the protocol or the input sampler - use augusta/grid.h or augusta::command instead:\n${core_report}\n"
  )
endif()
if(primitives_violations)
  list(JOIN primitives_violations "\n" primitives_report)
  string(
    APPEND report
    "The neutral primitives depend on another module - they must stay below the engine and the protocol alike:\n${primitives_report}\n"
  )
endif()
if(definition_violations)
  list(JOIN definition_violations "\n" definition_report)
  string(
    APPEND report
    "A counter or bound the primitives own is defined again - take it from augusta/primitives.h instead:\n${definition_report}\n"
  )
endif()
if(report)
  string(STRIP "${report}" report)
  message(FATAL_ERROR "${report}")
endif()
list(LENGTH headers count)
list(LENGTH primitives_headers primitives_count)
message(
  STATUS
  "core_boundary: ${count} headers free of protocol and input types, ${primitives_count} primitives headers free of other modules and defined nowhere else"
)

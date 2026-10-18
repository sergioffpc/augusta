# Tags each augusta_* module target (and the executables, augustac and
# augustad) with which side of the client/server split it belongs to, and
# checks that no tagged target's declared dependencies cross a boundary it
# isn't allowed to cross - turning docs/ARCHITECTURE.md §5's Shared
# Core/Client-only/Server-only split into a configure-time check instead of
# relying on code review to catch a client-only module linked into the server
# Runtime (or vice versa). Tagging a module under src/modules also checks it
# has the README.md stating its responsibility (docs/agents/coding-standards.md).
#
# Usage: call augusta_set_module_scope(<target> <SHARED|CLIENT_ONLY|
# SERVER_ONLY>) right after a module's own add_library() (or after an
# executable's own target_link_libraries() call), then call
# augusta_check_module_scopes() exactly once, after every tagged target has
# been defined.
include_guard(GLOBAL)

set_property(GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS "")

function(augusta_set_module_scope target scope)
  if(NOT scope MATCHES "^(SHARED|CLIENT_ONLY|SERVER_ONLY)$")
    message(
      FATAL_ERROR
      "augusta_set_module_scope(${target}): scope must be one of SHARED, CLIENT_ONLY, SERVER_ONLY - got '${scope}'"
    )
  endif()
  # Every module states its responsibility in a README.md beside its
  # CMakeLists.txt (docs/agents/coding-standards.md, Module README).
  # Executables, tagged from outside src/modules, are not modules.
  if(CMAKE_CURRENT_SOURCE_DIR MATCHES "/src/modules/[^/]+$" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    message(
      FATAL_ERROR
      "augusta_set_module_scope(${target}): ${CMAKE_CURRENT_SOURCE_DIR} has no README.md stating the module's responsibility"
    )
  endif()
  set_target_properties(${target} PROPERTIES AUGUSTA_MODULE_SCOPE ${scope})
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS ${target})
endfunction()

# Every direct dependency of target, whatever keyword declared it:
# LINK_LIBRARIES covers PUBLIC/PRIVATE, INTERFACE_LINK_LIBRARIES covers
# PUBLIC/INTERFACE (the latter is the only one an INTERFACE library target like
# augusta_math has). Sets out.
function(_augusta_target_dependencies target out)
  set(deps "")
  get_target_property(link_libs ${target} LINK_LIBRARIES)
  if(link_libs)
    list(APPEND deps ${link_libs})
  endif()
  get_target_property(iface_libs ${target} INTERFACE_LINK_LIBRARIES)
  if(iface_libs)
    list(APPEND deps ${iface_libs})
  endif()
  if(deps)
    list(REMOVE_DUPLICATES deps)
  endif()
  set(${out} "${deps}" PARENT_SCOPE)
endfunction()

function(augusta_check_module_scopes)
  get_property(modules GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS)
  list(REMOVE_DUPLICATES modules)

  foreach(target ${modules})
    # Not every tagged target exists on every platform (augusta_renderer
    # and augusta_client are only added with AUGUSTA_CLIENT - see the root
    # CMakeLists.txt).
    if(NOT TARGET ${target})
      continue()
    endif()
    get_target_property(scope ${target} AUGUSTA_MODULE_SCOPE)

    _augusta_target_dependencies(${target} deps)

    foreach(dep ${deps})
      # Most entries here are either generator expressions (e.g. flecs's
      # $<IF:$<TARGET_EXISTS:...>,...>) or third-party targets/link flags
      # with no AUGUSTA_MODULE_SCOPE of their own - only tagged augusta
      # targets are checked.
      if(NOT TARGET ${dep})
        continue()
      endif()
      get_target_property(dep_scope ${dep} AUGUSTA_MODULE_SCOPE)
      if(NOT dep_scope)
        continue()
      endif()

      set(ok FALSE)
      if(scope STREQUAL "SHARED" AND dep_scope STREQUAL "SHARED")
        set(ok TRUE)
      elseif(scope STREQUAL "CLIENT_ONLY" AND dep_scope MATCHES "^(SHARED|CLIENT_ONLY)$")
        set(ok TRUE)
      elseif(scope STREQUAL "SERVER_ONLY" AND dep_scope MATCHES "^(SHARED|SERVER_ONLY)$")
        set(ok TRUE)
      endif()

      if(NOT ok)
        message(
          FATAL_ERROR
          "Module dependency crosses the client/server split (docs/ARCHITECTURE.md #5): "
          "${target} (${scope}) depends on ${dep} (${dep_scope})"
        )
      endif()
    endforeach()
  endforeach()
endfunction()

# Executables are Glue (ADR-0060): an executable's only target is the
# executable itself; it links only the application, its Runtime, failure,
# logging and version; and no target links it, so Glue is tested only end to
# end, through the Runtime it runs. Mechanism that is worth a test of its own
# belongs to a module under src/modules/, where a test can link it without
# linking an executable. The same kind of check as the client/server split
# above - ARCHITECTURE.md §5 turned into a configure-time error rather than
# something a review has to catch.
#
# Usage: call augusta_set_executable_glue(<executable> <runtime>) at the end of
# that executable's own CMakeLists.txt, runtime naming a target of its Runtime's
# module, then augusta_check_executable_glue() exactly once, after every
# target - every test target included - has been defined.
#
# The Agent's Python module (tools/agent/native) is not registered: it is a
# Python tool's native part, which keeps the tools layout beside the agent
# Runtime it binds and is tested through its package (ADR-0060, Python tools).

set_property(GLOBAL PROPERTY AUGUSTA_GLUE_EXECUTABLES "")
set_property(GLOBAL PROPERTY AUGUSTA_GLUE_EVERY_BINARY "")

# Links target into every binary defined after the call - an executable, a
# module, a test alike - and lets an executable link it past the Glue check: it
# is the build's own infrastructure, such as a sanitizer's default options
# (ADR-0008, ADR-0013), which no Runtime could wire for it. A module is never
# linked this way: what every binary links is no reason to be Glue.
macro(augusta_link_every_binary target)
  link_libraries(${target})
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_GLUE_EVERY_BINARY ${target})
endmacro()

function(augusta_set_executable_glue executable runtime)
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_GLUE_EXECUTABLES ${executable})
  set_property(GLOBAL PROPERTY AUGUSTA_GLUE_DIRECTORY_${executable} "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(GLOBAL PROPERTY AUGUSTA_GLUE_RUNTIME_${executable} ${runtime})
endfunction()

# Every target defined in dir or in a directory dir adds; sets out.
function(_augusta_directory_targets dir out)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirectories DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory ${subdirectories})
    _augusta_directory_targets("${subdirectory}" subdirectory_targets)
    list(APPEND targets ${subdirectory_targets})
  endforeach()
  set(${out} "${targets}" PARENT_SCOPE)
endfunction()

function(augusta_check_executable_glue)
  get_property(executables GLOBAL PROPERTY AUGUSTA_GLUE_EXECUTABLES)
  list(REMOVE_DUPLICATES executables)

  # What every executable may link besides its Runtime's module, and what every
  # binary links (augusta_link_every_binary).
  get_property(every_binary GLOBAL PROPERTY AUGUSTA_GLUE_EVERY_BINARY)
  set(
    allowed
    augusta_application
    augusta_failure
    augusta_logging
    augusta_version
    ${every_binary}
  )
  set(violations "")
  set(links "")
  set(linkages "")

  foreach(executable ${executables})
    get_property(directory GLOBAL PROPERTY AUGUSTA_GLUE_DIRECTORY_${executable})
    get_property(runtime GLOBAL PROPERTY AUGUSTA_GLUE_RUNTIME_${executable})
    if(NOT TARGET ${runtime})
      message(FATAL_ERROR "augusta_set_executable_glue(${executable}): its Runtime ${runtime} is not a target")
    endif()

    # A target's location is its own source directory, never its name, so a
    # target cannot dodge the rule by what it is called.
    _augusta_directory_targets("${directory}" targets)
    foreach(target ${targets})
      if(NOT "${target}" STREQUAL "${executable}")
        get_target_property(source_dir ${target} SOURCE_DIR)
        file(RELATIVE_PATH relative "${CMAKE_SOURCE_DIR}" "${source_dir}")
        list(APPEND violations "  ${target} (${relative})")
      endif()
    endforeach()

    # Its Runtime is every target of the Runtime's module, its application
    # among them. Only targets are judged: a system library such as winmm, a
    # link flag or a generator expression is not a module.
    get_target_property(runtime_dir ${runtime} SOURCE_DIR)
    _augusta_target_dependencies(${executable} deps)
    foreach(dep ${deps})
      if(NOT TARGET ${dep} OR "${dep}" IN_LIST allowed)
        continue()
      endif()
      get_target_property(dep_dir ${dep} SOURCE_DIR)
      if(NOT "${dep_dir}" STREQUAL "${runtime_dir}")
        list(APPEND links "  ${executable} -> ${dep}")
      endif()
    endforeach()
  endforeach()

  # No target links an executable: an end-to-end test links its Runtime.
  _augusta_directory_targets("${CMAKE_SOURCE_DIR}" everyone)
  foreach(target ${everyone})
    _augusta_target_dependencies(${target} deps)
    foreach(dep ${deps})
      if("${dep}" IN_LIST executables)
        list(APPEND linkages "  ${target} -> ${dep}")
      endif()
    endforeach()
  endforeach()

  set(report "")
  if(violations)
    list(JOIN violations "\n" joined)
    string(
      APPEND report
      "An executable's only target is the executable. These targets sit inside an executable's directory - "
      "give each a module under src/modules/:\n${joined}\n"
    )
  endif()
  if(links)
    list(JOIN links "\n" joined)
    string(
      APPEND report
      "An executable links only the application, its Runtime, failure, logging and version. These links reach "
      "past them - let its Runtime wire the module instead:\n${joined}\n"
    )
  endif()
  if(linkages)
    list(JOIN linkages "\n" joined)
    string(
      APPEND report
      "Glue is tested only end to end, through the Runtime it runs: no target links an executable. These do - "
      "link the executable's Runtime instead:\n${joined}\n"
    )
  endif()
  if(report)
    string(STRIP "${report}" report)
    message(FATAL_ERROR "Executables are Glue (ADR-0060, docs/adr/0060-executables-are-glue.md):\n${report}")
  endif()
endfunction()

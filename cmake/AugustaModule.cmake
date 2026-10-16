# Tags each augusta_* module target (and the runtime targets and executables,
# augustac_runtime/augustad_runtime and augustac/augustad) with which side
# of the client/server split it belongs to, and checks that no tagged
# target's declared dependencies cross a boundary it isn't allowed to
# cross - turning
# docs/ARCHITECTURE.md §5's Shared Core/Client-only/Server-only split
# into a configure-time check instead of relying on code review to catch
# a client-only module linked into augustad_runtime (or vice versa).
#
# Usage: call augusta_set_module_scope(<target> <SHARED|CLIENT_ONLY|
# SERVER_ONLY>) right after a module's own add_library() (or after a
# runtime target's or executable's own target_link_libraries() call),
# then call augusta_check_module_scopes() exactly once, after every
# tagged target has been defined.
include_guard(GLOBAL)

set_property(GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS "")

function(augusta_set_module_scope target scope)
  if(NOT scope MATCHES "^(SHARED|CLIENT_ONLY|SERVER_ONLY)$")
    message(
      FATAL_ERROR
      "augusta_set_module_scope(${target}): scope must be one of SHARED, CLIENT_ONLY, SERVER_ONLY - got '${scope}'"
    )
  endif()
  set_target_properties(${target} PROPERTIES AUGUSTA_MODULE_SCOPE ${scope})
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS ${target})
endfunction()

function(augusta_check_module_scopes)
  get_property(modules GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS)
  list(REMOVE_DUPLICATES modules)

  foreach(target ${modules})
    # Not every tagged target exists on every platform (augusta_renderer
    # and augustac_runtime are only added with AUGUSTA_CLIENT - see the root
    # CMakeLists.txt).
    if(NOT TARGET ${target})
      continue()
    endif()
    get_target_property(scope ${target} AUGUSTA_MODULE_SCOPE)

    # LINK_LIBRARIES covers PUBLIC/PRIVATE deps, INTERFACE_LINK_LIBRARIES
    # covers PUBLIC/INTERFACE deps (the latter is the only one an
    # INTERFACE library target like augusta_math has) - union both to get
    # every direct dependency regardless of the keyword it was declared
    # with.
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

# Executables are Glue (ADR-0060): an executable's own directory holds its
# entry point, its application and its runtime, and nothing else. Mechanism
# that is worth a test of its own belongs to a module under src/modules/,
# where a test can link it without linking an executable. The same kind of
# check as the client/server split above - ARCHITECTURE.md §5 turned into a
# configure-time error rather than something a review has to catch.
#
# Usage: call augusta_set_executable_glue(<executable>) at the end of that
# executable's own CMakeLists.txt, then call augusta_check_executable_glue()
# exactly once, after every target has been defined.
#
# AUGUSTA_GLUE_EXCEPTIONS_<executable> is the migration's ledger: the library
# targets that sit inside that executable today, in the order its
# CMakeLists.txt defines them. It is only ever edited by removal - each
# migration takes its own entries out, an entry naming a library that is no
# longer there fails the configure, and the lists and this check go away
# together once both are empty.

set(
  AUGUSTA_GLUE_EXCEPTIONS_augustad
  augustad_parameters_loader
  augustad_policy_loader
  augustad_content
  augustad_match
  augustad_commands
  augustad_misbehaviour
  augustad_admission
  augustad_heartbeat
  augustad_liveness
  augustad_metrics
  augustad_connection_health
  augustad_host
  augustad_crash
)

set(
  AUGUSTA_GLUE_EXCEPTIONS_augustac
  augustac_content
  augustac_frame_mapping
  augustac_net_stats
  augustac_lobby_readiness
)

set_property(GLOBAL PROPERTY AUGUSTA_GLUE_EXECUTABLES "")

function(augusta_set_executable_glue executable)
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_GLUE_EXECUTABLES ${executable})
  set_property(GLOBAL PROPERTY AUGUSTA_GLUE_DIRECTORY_${executable} "${CMAKE_CURRENT_SOURCE_DIR}")
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

  set(violations "")
  set(stale "")
  set(remaining 0)

  foreach(executable ${executables})
    get_property(directory GLOBAL PROPERTY AUGUSTA_GLUE_DIRECTORY_${executable})
    # The three Glue targets an executable's directory is allowed: itself, its
    # application and its runtime.
    set(glue ${executable} ${executable}_application ${executable}_runtime)
    set(exceptions ${AUGUSTA_GLUE_EXCEPTIONS_${executable}})
    list(LENGTH exceptions count)
    math(EXPR remaining "${remaining} + ${count}")

    # A target's location is its own source directory, never its name, so a
    # library cannot dodge the rule by what it is called. Only library targets
    # are judged: the custom targets a directory adds for staging, debug info
    # or installation are not Mechanism.
    _augusta_directory_targets("${directory}" targets)
    set(libraries "")
    foreach(target ${targets})
      get_target_property(type ${target} TYPE)
      if(type MATCHES "_LIBRARY$")
        list(APPEND libraries ${target})
      endif()
    endforeach()

    foreach(target ${libraries})
      if("${target}" IN_LIST glue)
        continue()
      endif()
      if("${target}" IN_LIST exceptions)
        continue()
      endif()
      get_target_property(source_dir ${target} SOURCE_DIR)
      file(RELATIVE_PATH relative "${CMAKE_SOURCE_DIR}" "${source_dir}")
      list(APPEND violations "  ${target} (${relative})")
    endforeach()

    # An entry naming a library that is no longer there has been migrated: the
    # ledger says otherwise, so the configure fails until the entry goes too.
    foreach(exception ${exceptions})
      if(NOT "${exception}" IN_LIST libraries)
        list(APPEND stale "  ${exception} (${executable})")
      endif()
    endforeach()
  endforeach()

  set(report "")
  if(violations)
    list(JOIN violations "\n" joined)
    string(
      APPEND report
      "An executable's directory holds only its entry point, its application and its runtime. "
      "These library targets sit inside one - give each a module under src/modules/:\n${joined}\n"
    )
  endif()
  if(stale)
    list(JOIN stale "\n" joined)
    string(
      APPEND report
      "These exceptions name libraries that no longer sit inside the executable they are listed "
      "under - take each entry out of AUGUSTA_GLUE_EXCEPTIONS_<executable> in "
      "cmake/AugustaModule.cmake:\n${joined}\n"
    )
  endif()
  if(report)
    string(STRIP "${report}" report)
    message(FATAL_ERROR "Executables are Glue (ADR-0060, docs/adr/0060-executables-are-glue.md):\n${report}")
  endif()

  # The migration's progress, so it reads off a build log.
  message(STATUS "Executables are Glue (ADR-0060): ${remaining} exception(s) left inside an executable")
endfunction()

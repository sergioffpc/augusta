# Tags each augusta_* module target (and the two runtime targets,
# augustac_runtime/augustad_runtime) with which side of the client/server
# split it belongs to, and checks that no tagged target's declared
# dependencies cross a boundary it isn't allowed to cross - turning
# docs/ARCHITECTURE.md §5's Shared Core/Client-only/Server-only split
# into a configure-time check instead of relying on code review to catch
# a client-only module linked into augustad_runtime (or vice versa).
#
# Usage: call augusta_set_module_scope(<target> <SHARED|CLIENT_ONLY|
# SERVER_ONLY>) right after a module's own add_library() (or after
# augustac_runtime/augustad_runtime's own target_link_libraries() call),
# then call augusta_check_module_scopes() exactly once, after every
# module and both runtime targets have been defined.
include_guard(GLOBAL)

set_property(GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS "")

function(augusta_set_module_scope target scope)
  if (NOT scope MATCHES "^(SHARED|CLIENT_ONLY|SERVER_ONLY)$")
    message(FATAL_ERROR "augusta_set_module_scope(${target}): scope must be one of SHARED, CLIENT_ONLY, SERVER_ONLY - got '${scope}'")
  endif()
  set_target_properties(${target} PROPERTIES AUGUSTA_MODULE_SCOPE ${scope})
  set_property(GLOBAL APPEND PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS ${target})
endfunction()

function(augusta_check_module_scopes)
  get_property(modules GLOBAL PROPERTY AUGUSTA_MODULE_SCOPE_TARGETS)
  list(REMOVE_DUPLICATES modules)

  foreach(target ${modules})
    # Not every tagged target exists on every platform (augusta_renderer
    # and augustac_runtime are only added under WIN32 - see the root
    # CMakeLists.txt).
    if (NOT TARGET ${target})
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
    if (link_libs)
      list(APPEND deps ${link_libs})
    endif()
    get_target_property(iface_libs ${target} INTERFACE_LINK_LIBRARIES)
    if (iface_libs)
      list(APPEND deps ${iface_libs})
    endif()
    if (deps)
      list(REMOVE_DUPLICATES deps)
    endif()

    foreach(dep ${deps})
      # Most entries here are either generator expressions (e.g. flecs's
      # $<IF:$<TARGET_EXISTS:...>,...>) or third-party targets/link flags
      # with no AUGUSTA_MODULE_SCOPE of their own - only tagged augusta
      # targets are checked.
      if (NOT TARGET ${dep})
        continue()
      endif()
      get_target_property(dep_scope ${dep} AUGUSTA_MODULE_SCOPE)
      if (NOT dep_scope)
        continue()
      endif()

      set(ok FALSE)
      if (scope STREQUAL "SHARED" AND dep_scope STREQUAL "SHARED")
        set(ok TRUE)
      elseif (scope STREQUAL "CLIENT_ONLY" AND dep_scope MATCHES "^(SHARED|CLIENT_ONLY)$")
        set(ok TRUE)
      elseif (scope STREQUAL "SERVER_ONLY" AND dep_scope MATCHES "^(SHARED|SERVER_ONLY)$")
        set(ok TRUE)
      endif()

      if (NOT ok)
        message(FATAL_ERROR
          "Module dependency crosses the client/server split (docs/ARCHITECTURE.md #5): "
          "${target} (${scope}) depends on ${dep} (${dep_scope})")
      endif()
    endforeach()
  endforeach()
endfunction()

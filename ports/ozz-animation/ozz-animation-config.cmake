# ozz-animation's libraries as imported targets, which upstream installs no
# package for: ozz::base, ozz::animation (the runtime), ozz::animation_offline
# (what builds its archives) and ozz::geometry (skinning), each linking the one
# it builds on.
get_filename_component(_ozz_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# name: the library's, e.g. ozz_base; target: the imported target's, e.g.
# ozz::base; dependency: the target it links, or none.
function(_ozz_add_library name target dependency)
  if(TARGET ${target})
    return()
  endif()
  set(file "${CMAKE_STATIC_LIBRARY_PREFIX}${name}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  add_library(${target} STATIC IMPORTED)
  set_target_properties(
    ${target}
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_ozz_prefix}/include"
      IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
      IMPORTED_LOCATION_RELEASE "${_ozz_prefix}/lib/${file}"
      IMPORTED_LOCATION_DEBUG "${_ozz_prefix}/debug/lib/${file}"
      IMPORTED_LOCATION "${_ozz_prefix}/lib/${file}"
  )
  if(dependency)
    set_target_properties(${target} PROPERTIES INTERFACE_LINK_LIBRARIES ${dependency})
  endif()
endfunction()

_ozz_add_library(ozz_base ozz::base "")
_ozz_add_library(ozz_animation ozz::animation ozz::base)
_ozz_add_library(ozz_animation_offline ozz::animation_offline ozz::animation)
_ozz_add_library(ozz_geometry ozz::geometry ozz::base)

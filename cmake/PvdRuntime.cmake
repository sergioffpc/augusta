# OmniPVD's runtime (PVDRuntime_64), which the PhysX overlay port builds
# (ports/physx): PhysX loads it by its file name when debug.physics_recording
# starts a recording, so it must sit beside the executable. Only a Debug
# build's PhysX records (a non-Debug one compiles OmniPVD out), and only a
# Development build knows the key (ADR-0074), so only such a build stages it.
include_guard(GLOBAL)

set(augusta_pvd_runtime "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND AUGUSTA_DEBUG_TOOLS)
  # Windows' dynamic triplet puts the DLL in bin, as every port's; the static
  # Linux triplet puts the shared library in lib (ports/physx/portfile.cmake).
  if(WIN32)
    set(augusta_pvd_runtime "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/PVDRuntime_64.dll")
  else()
    set(augusta_pvd_runtime "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib/libPVDRuntime_64.so")
  endif()
endif()
set(AUGUSTA_PVD_RUNTIME "${augusta_pvd_runtime}" CACHE INTERNAL "OmniPVD's runtime library, empty when not staged")

# Copies OmniPVD's runtime beside target, an executable whose physics can
# record, after each build; does nothing where it is not staged. On Linux the
# executable then finds it there ($ORIGIN), as PhysX opens it by name alone.
function(augusta_stage_pvd_runtime target)
  if(NOT AUGUSTA_PVD_RUNTIME)
    return()
  endif()
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AUGUSTA_PVD_RUNTIME} $<TARGET_FILE_DIR:${target}>
    COMMENT "Staging OmniPVD's runtime beside ${target}"
    VERBATIM
  )
  if(NOT WIN32)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
  endif()
endfunction()

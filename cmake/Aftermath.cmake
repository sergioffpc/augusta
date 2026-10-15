# NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture), on Windows
# and Linux. It is EULA-gated on NVIDIA's developer site, so nothing fetches it:
# it is autodetected from an installed NVIDIA Nsight Graphics, which bundles it
# at "<Nsight Graphics>/SDKs/NsightAftermathSDK/<version>/". Nsight Graphics
# installs under "%ProgramFiles%\NVIDIA Corporation\Nsight Graphics <version>"
# on Windows and "/opt/nvidia/nsight-graphics-for-linux/<version>" on Linux;
# both version segments float, hence the glob. If several installs or SDK
# versions sit side by side, the lexicographically last wins: not a true
# version sort, but the version strings seen in practice (YYYY.M.build.rev)
# compare correctly that way. No Nsight Graphics installed means no Aftermath:
# there is nowhere else this looks, and the build and the client are unchanged.
#
# Sets AUGUSTA_AFTERMATH_DIR to the SDK's root, empty when none is found, and
# AUGUSTA_AFTERMATH_RUNTIME_LIBRARIES to the shared libraries an executable
# linking it loads, both cached so augusta_stage_aftermath sees them from any
# directory. With the SDK, the imported target augusta_aftermath links it.
include_guard(GLOBAL)

if(WIN32)
  set(nsight_graphics_installs "$ENV{ProgramFiles}/NVIDIA Corporation/Nsight Graphics *")
  set(aftermath_library GFSDK_Aftermath_Lib.x64.dll)
  set(aftermath_runtime_pattern "*.dll")
else()
  set(nsight_graphics_installs "/opt/nvidia/nsight-graphics-for-linux/*")
  set(aftermath_library libGFSDK_Aftermath_Lib.x64.so)
  set(aftermath_runtime_pattern "*.so*")
endif()

set(aftermath_dir "")
set(aftermath_libraries "")
file(GLOB aftermath_candidates "${nsight_graphics_installs}/SDKs/NsightAftermathSDK/*")
set(aftermath_dirs "")
foreach(candidate IN LISTS aftermath_candidates)
  if(EXISTS "${candidate}/include/GFSDK_Aftermath.h" AND EXISTS "${candidate}/lib/x64/${aftermath_library}")
    list(APPEND aftermath_dirs "${candidate}")
  endif()
endforeach()
if(aftermath_dirs)
  list(SORT aftermath_dirs)
  list(GET aftermath_dirs -1 aftermath_dir)
  # Globbed rather than a fixed GFSDK_Aftermath_Lib.x64 (+ llvm_7_0_1 on older
  # SDKs) list: the set an SDK ships has changed between versions.
  file(GLOB aftermath_libraries "${aftermath_dir}/lib/x64/${aftermath_runtime_pattern}")
  message(STATUS "Nsight Aftermath SDK found at ${aftermath_dir}")
  add_library(augusta_aftermath SHARED IMPORTED GLOBAL)
  set_target_properties(
    augusta_aftermath
    PROPERTIES
      IMPORTED_LOCATION ${aftermath_dir}/lib/x64/${aftermath_library}
      INTERFACE_INCLUDE_DIRECTORIES ${aftermath_dir}/include
  )
  if(WIN32)
    set_target_properties(
      augusta_aftermath
      PROPERTIES IMPORTED_IMPLIB ${aftermath_dir}/lib/x64/GFSDK_Aftermath_Lib.x64.lib
    )
  else()
    # Linked by name (-l), so the executable needs the library by its file
    # name, which augustac's $ORIGIN finds beside it, and never by the SDK's
    # path, whether or not the library names itself (DT_SONAME).
    set_target_properties(augusta_aftermath PROPERTIES IMPORTED_NO_SONAME TRUE)
  endif()
endif()
set(AUGUSTA_AFTERMATH_DIR "${aftermath_dir}" CACHE INTERNAL "Nsight Aftermath SDK's root, empty without one")
set(
  AUGUSTA_AFTERMATH_RUNTIME_LIBRARIES
  "${aftermath_libraries}"
  CACHE INTERNAL
  "Nsight Aftermath SDK's runtime shared libraries"
)

# Copies the Aftermath shared libraries beside target, an executable that links
# a renderer built with Aftermath, after each build; does nothing without the
# SDK.
function(augusta_stage_aftermath target)
  if(AUGUSTA_AFTERMATH_RUNTIME_LIBRARIES)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AUGUSTA_AFTERMATH_RUNTIME_LIBRARIES} $<TARGET_FILE_DIR:${target}>
      COMMENT "Staging the Nsight Aftermath libraries beside ${target}"
      VERBATIM
    )
  endif()
endfunction()

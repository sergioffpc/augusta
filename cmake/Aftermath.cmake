# NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture). It is
# EULA-gated on NVIDIA's developer site, so nothing fetches it: it is
# autodetected from an installed NVIDIA Nsight Graphics, which bundles it at
# "...\Nsight Graphics <version>\SDKs\NsightAftermathSDK\<version>\" - both
# version segments float, hence the glob. If several installs or SDK versions
# sit side by side, the lexicographically last wins: not a true version sort,
# but the version strings seen in practice (YYYY.M.build.rev) compare correctly
# that way. No Nsight Graphics installed means no Aftermath: there is nowhere
# else this looks, and the build and the client are unchanged.
#
# Sets AUGUSTA_AFTERMATH_DIR to the SDK's root, empty when none is found, and
# AUGUSTA_AFTERMATH_RUNTIME_DLLS to the DLLs an executable linking it loads,
# both cached so augusta_stage_aftermath sees them from any directory. Both
# renderers read these until Falcor's goes (ADR-0059).
include_guard(GLOBAL)

set(aftermath_dir "")
set(aftermath_dlls "")
file(GLOB aftermath_candidates "$ENV{ProgramFiles}/NVIDIA Corporation/Nsight Graphics */SDKs/NsightAftermathSDK/*")
set(aftermath_dirs "")
foreach(candidate IN LISTS aftermath_candidates)
  if(EXISTS "${candidate}/include/GFSDK_Aftermath.h")
    list(APPEND aftermath_dirs "${candidate}")
  endif()
endforeach()
if(aftermath_dirs)
  list(SORT aftermath_dirs)
  list(GET aftermath_dirs -1 aftermath_dir)
  # Globbed rather than a fixed GFSDK_Aftermath_Lib.x64.dll (+ llvm_7_0_1.dll
  # on older SDKs) list: the set an SDK ships has changed between versions
  # (the llvm one is gone as of the 2026.3 SDK, statically linked in instead).
  file(GLOB aftermath_dlls "${aftermath_dir}/lib/x64/*.dll")
  message(STATUS "Nsight Aftermath SDK found at ${aftermath_dir}")
endif()
set(AUGUSTA_AFTERMATH_DIR "${aftermath_dir}" CACHE INTERNAL "Nsight Aftermath SDK's root, empty without one")
set(AUGUSTA_AFTERMATH_RUNTIME_DLLS "${aftermath_dlls}" CACHE INTERNAL "Nsight Aftermath SDK's runtime DLLs")

# Copies the Aftermath DLLs beside target, an executable that links a renderer
# built with Aftermath, after each build; does nothing without the SDK.
function(augusta_stage_aftermath target)
  if(AUGUSTA_AFTERMATH_RUNTIME_DLLS)
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AUGUSTA_AFTERMATH_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
      COMMENT "Staging the Nsight Aftermath DLLs beside ${target}"
      VERBATIM
    )
  endif()
endfunction()

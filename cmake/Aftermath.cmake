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
# Sets AUGUSTA_AFTERMATH_DIR to the SDK's root, empty when none is found,
# cached so it is seen from any directory. With the SDK, the imported target
# augusta_aftermath links it. No target includes this yet: the Renderer
# attaches no Aftermath until it does so on its Vulkan device (ADR-0059), and
# then whatever links it stages the SDK's DLLs beside it.
include_guard(GLOBAL)

set(aftermath_dir "")
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
  message(STATUS "Nsight Aftermath SDK found at ${aftermath_dir}")
  add_library(augusta_aftermath SHARED IMPORTED GLOBAL)
  set_target_properties(
    augusta_aftermath
    PROPERTIES
      IMPORTED_IMPLIB ${aftermath_dir}/lib/x64/GFSDK_Aftermath_Lib.x64.lib
      IMPORTED_LOCATION ${aftermath_dir}/lib/x64/GFSDK_Aftermath_Lib.x64.dll
      INTERFACE_INCLUDE_DIRECTORIES ${aftermath_dir}/include
  )
endif()
set(AUGUSTA_AFTERMATH_DIR "${aftermath_dir}" CACHE INTERNAL "Nsight Aftermath SDK's root, empty without one")

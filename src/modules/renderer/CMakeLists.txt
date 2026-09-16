add_library(augusta_renderer STATIC
  renderer.cpp
)
target_include_directories(augusta_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer PUBLIC augusta_input PRIVATE nvtx3-cpp)

if (WIN32)
  # NVIDIA Falcor (ADR-0009) is vendored as a pinned git submodule
  # (ADR-0025, third_party/falcor, plain upstream - no fork) rather than
  # pulled in via add_subdirectory(): its root CMakeLists.txt relies on
  # ${CMAKE_SOURCE_DIR} throughout (packman fetch paths, its own external/
  # subdirectory, ...), which would resolve to *this* repo's root instead
  # of Falcor's own if embedded, breaking its build. Instead it's built
  # standalone via its own CMakePresets.json preset -
  # "windows-ninja-msvc-augusta", added by cmake/patches/falcor-augusta.patch
  # (ADR-0009 anticipated needing a small patch to the vendored copy; see
  # cmake/patches/ for augusta's own third-party patches/overrides generally),
  # which pins CMAKE_POLICY_VERSION_MINIMUM for its vendored (old) pybind11
  # copy against newer CMake, and turns on CMAKE_EXPORT_COMPILE_COMMANDS so
  # the exact flags below could be lifted verbatim from a real,
  # verified-working compile of a Falcor sample. The same patch also
  # retunes Source/Falcor/Utils/Timing/ProfilerUI.{h,cpp}'s HUD-only
  # defaults (graph on/CPU time/no averaging, bright shades-of-green event
  # palette) per the user's request - those two fields/that palette are
  # private to ProfilerUI with no public setter, so there's no way to
  # reach them from augusta's own code without this. The submodule
  # checkout itself stays a pristine, unmodified upstream commit - the
  # patch is applied fresh (over a hard reset covering every file it
  # touches, so this is idempotent across reconfigures) rather than
  # committed into it.
  find_package(Git REQUIRED)

  set(FALCOR_DIR ${CMAKE_SOURCE_DIR}/third_party/falcor)
  set(FALCOR_PRESET windows-ninja-msvc-augusta)
  set(FALCOR_BUILD_DIR ${FALCOR_DIR}/build/${FALCOR_PRESET})
  set(FALCOR_BIN_DIR ${FALCOR_BUILD_DIR}/bin/${CMAKE_BUILD_TYPE} CACHE INTERNAL "Falcor's own build output directory")
  set(FALCOR_DIR ${FALCOR_DIR} CACHE INTERNAL "Vendored Falcor submodule's root directory")

  # NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture) - unlike
  # Falcor's other binary deps it isn't packman-fetchable (EULA-gated on
  # NVIDIA's developer site), so it can't be fetched here the way those are.
  # Instead it's autodetected from an installed NVIDIA Nsight Graphics,
  # which bundles the Aftermath SDK as a component (confirmed present at
  # "...\Nsight Graphics <version>\SDKs\NsightAftermathSDK\<version>\" on a
  # real install - both version segments float, hence the globs below). If
  # several Nsight Graphics installs/SDK versions are present side by side,
  # the lexicographically last one wins - not a true version-sort, but the
  # version strings observed in practice (YYYY.M.build.rev) happen to
  # compare correctly that way. No Nsight Graphics installed means no
  # Aftermath support - there's nowhere else this looks.
  # Falcor's own external/CMakeLists.txt autodetects Aftermath purely from
  # ${FALCOR_DIR}/external/packman/aftermath/include/GFSDK_Aftermath.h
  # existing at *its* configure time, so the custom command below syncs the
  # discovered directory into that path first. FALCOR_HAS_AFTERMATH in
  # target_compile_definitions further down has to track this same
  # condition: it's an augusta-side compile definition for augusta_renderer's
  # own TUs (which pull in Falcor's headers directly, e.g. Device::Desc's
  # enableAftermath field and the AftermathContext member it gates on
  # Device), and a mismatch against what Falcor.lib itself was actually built
  # with would desync struct layouts between this target's object code and
  # Falcor.lib's.
  file(GLOB AUGUSTA_AFTERMATH_SYSTEM_CANDIDATES
    "$ENV{ProgramFiles}/NVIDIA Corporation/Nsight Graphics */SDKs/NsightAftermathSDK/*"
  )
  set(AUGUSTA_AFTERMATH_SYSTEM_DIRS "")
  foreach(candidate ${AUGUSTA_AFTERMATH_SYSTEM_CANDIDATES})
    if (EXISTS "${candidate}/include/GFSDK_Aftermath.h")
      list(APPEND AUGUSTA_AFTERMATH_SYSTEM_DIRS "${candidate}")
    endif()
  endforeach()
  if (AUGUSTA_AFTERMATH_SYSTEM_DIRS)
    list(SORT AUGUSTA_AFTERMATH_SYSTEM_DIRS)
    list(GET AUGUSTA_AFTERMATH_SYSTEM_DIRS -1 AUGUSTA_AFTERMATH_DIR)
    set(AUGUSTA_AFTERMATH_HEADER ${AUGUSTA_AFTERMATH_DIR}/include/GFSDK_Aftermath.h)
  endif()

  if (AUGUSTA_AFTERMATH_HEADER AND EXISTS ${AUGUSTA_AFTERMATH_HEADER})
    message(STATUS "Nsight Aftermath SDK found at ${AUGUSTA_AFTERMATH_DIR} - enabling FALCOR_HAS_AFTERMATH")
    set(FALCOR_HAS_AFTERMATH_VALUE 1)
    set(FALCOR_AFTERMATH_SYNC_COMMAND
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${AUGUSTA_AFTERMATH_DIR}" ${FALCOR_DIR}/external/packman/aftermath)
    # Falcor's own deploy_dependencies target (which would normally copy
    # the runtime DLL(s) next to Falcor.dll) is never built here - the
    # custom command below only builds `--target Falcor` (see its own
    # comment) - so they're copied next to Falcor.dll directly instead,
    # from where src/client/CMakeLists.txt's own POST_BUILD step already
    # picks up everything under FALCOR_BIN_DIR. Globbed rather than a fixed
    # GFSDK_Aftermath_Lib.x64.dll (+ llvm_7_0_1.dll on older SDKs) filename
    # list: the exact set of runtime DLLs an SDK ships has changed between
    # versions (the llvm one is gone as of the 2026.3 SDK, statically
    # linked in instead), and this way it doesn't need updating either way.
    file(GLOB AUGUSTA_AFTERMATH_RUNTIME_DLLS "${AUGUSTA_AFTERMATH_DIR}/lib/x64/*.dll")
    if (AUGUSTA_AFTERMATH_RUNTIME_DLLS)
      set(FALCOR_AFTERMATH_DEPLOY_COMMAND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AUGUSTA_AFTERMATH_RUNTIME_DLLS} ${FALCOR_BIN_DIR})
    endif()
    set(FALCOR_AFTERMATH_DEPENDS ${AUGUSTA_AFTERMATH_HEADER})
  else()
    set(FALCOR_HAS_AFTERMATH_VALUE 0)
  endif()

  set(FALCOR_STAMP ${CMAKE_CURRENT_BINARY_DIR}/falcor.stamp)
  add_custom_command(
    OUTPUT ${FALCOR_STAMP}
    COMMAND ${GIT_EXECUTABLE} -C ${FALCOR_DIR} checkout -- CMakeLists.txt CMakePresets.json
      Source/Falcor/Utils/Timing/ProfilerUI.h Source/Falcor/Utils/Timing/ProfilerUI.cpp
    COMMAND ${GIT_EXECUTABLE} -C ${FALCOR_DIR} apply ${CMAKE_SOURCE_DIR}/cmake/patches/falcor-augusta.patch
    # windows-ninja-msvc-base's CMAKE_MAKE_PROGRAM points at
    # tools/.packman/ninja/ninja.exe, needed immediately by project() - but
    # Falcor's own root CMakeLists.txt only pulls packman dependencies
    # *after* project(), too late for this on a fresh checkout. Falcor's
    # documented fix is setup.bat, pulling the *whole* dependencies.xml
    # first - but that runs packman pull against external/packman/* (e.g.
    # slang) a second time in a row once Falcor's own CMakeLists.txt does
    # its own pull moments later, and packman isn't reliably idempotent
    # against its own immediately-preceding run for at least that entry
    # ("Link ... cannot be created because it already exists as a
    # different type of file"). Pulling only the tools/.packman/* entries
    # here - via a copy of dependencies.xml's own <dependency name="tools_*">
    # elements (cmake/patches/falcor-tools-dependencies.xml) - satisfies
    # project()'s immediate need without ever touching external/packman/*
    # twice. packman resolves each entry's linkPath relative to the
    # project file's own location, so the copy has to land inside
    # ${FALCOR_DIR} for tools/.packman/ninja to end up in the right place.
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/cmake/patches/falcor-tools-dependencies.xml ${FALCOR_DIR}/tools-dependencies.xml
    COMMAND ${FALCOR_DIR}/tools/packman/packman.cmd pull ${FALCOR_DIR}/tools-dependencies.xml --platform windows-x86_64
    # -DCMAKE_POLICY_VERSION_MINIMUM is redundant with the preset's own
    # cacheVariables entry (both set it) but not with what actually
    # happens in practice: FALCOR_BUILD_DIR is shared across every one of
    # augusta's own outer presets (windows, windows-debug, ...), so the
    # first build under a *second* outer preset reconfigures an
    # already-cached Falcor build tree - and CMake's own "cache variable
    # changed, regenerating" path for an existing cache doesn't reliably
    # re-derive preset-only cacheVariables, silently dropping this one and
    # reviving the pybind11 CMAKE_POLICY_VERSION_MINIMUM error. Passing it
    # explicitly here makes it immune to that, regardless of which path
    # triggers the reconfigure.
    ${FALCOR_AFTERMATH_SYNC_COMMAND}
    COMMAND ${CMAKE_COMMAND} -S ${FALCOR_DIR} --preset ${FALCOR_PRESET} -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    COMMAND ${CMAKE_COMMAND} --build ${FALCOR_BUILD_DIR} --config ${CMAKE_BUILD_TYPE} --target Falcor
    ${FALCOR_AFTERMATH_DEPLOY_COMMAND}
    COMMAND ${CMAKE_COMMAND} -E touch ${FALCOR_STAMP}
    WORKING_DIRECTORY ${FALCOR_DIR}
    COMMENT "Patching and building vendored Falcor (ADR-0009) - slow on a clean checkout"
    # Without these, Ninja's static graph validation has no rule for
    # Falcor.lib/Falcor.dll/fmt.lib (only falcor.stamp is a declared
    # OUTPUT) and fails outright ("missing and no known rule to make it")
    # on a build where they don't already exist - the add_dependencies()
    # order-only edge below isn't enough on its own. fmt.lib is genuinely
    # built from source by the `--target Falcor` step, same as
    # Falcor.lib/Falcor.dll. python310.lib/slang.lib/gfx.lib are NOT
    # listed here on purpose - see the comment by their target_link_options
    # use further down for why.
    BYPRODUCTS
      "${FALCOR_BUILD_DIR}/Source/Falcor/${CMAKE_BUILD_TYPE}/Falcor.lib"
      "${FALCOR_BIN_DIR}/Falcor.dll"
      "${FALCOR_BUILD_DIR}/external/fmt/${CMAKE_BUILD_TYPE}/$<IF:$<CONFIG:Debug>,fmtd,fmt>.lib"
    # Ninja only re-runs a custom command when one of its declared
    # DEPENDS changes (or the command line itself does) - without this,
    # editing the patch's *content* (e.g. tweaking a color inside
    # ProfilerUI.cpp's patched hunk) left falcor.stamp looking up to date
    # and silently skipped re-applying it, even though `git apply` above
    # references the file.
    DEPENDS
      ${CMAKE_SOURCE_DIR}/cmake/patches/falcor-augusta.patch
      ${CMAKE_SOURCE_DIR}/cmake/patches/falcor-tools-dependencies.xml
      ${FALCOR_AFTERMATH_DEPENDS}
    VERBATIM
  )
  add_custom_target(falcor_build DEPENDS ${FALCOR_STAMP})

  # Falcor's own PUBLIC link/include/compile-definition requirements (fmt,
  # Vulkan-Headers, slang/gfx, imgui, ...) - copied verbatim from
  # compile_commands.json / the ninja link edge for
  # Source/Samples/SampleAppTemplate, a real consumer of `Falcor` built
  # the same way this target will be, rather than re-derived by hand from
  # Source/Falcor/CMakeLists.txt's own target_link_libraries/
  # target_compile_definitions.
  #
  # Applied directly to augusta_renderer rather than parked on a separate
  # `add_library(falcor ... IMPORTED)` target: CMake validates that an
  # IMPORTED target's INTERFACE_INCLUDE_DIRECTORIES already exist on disk
  # at configure time and hard-errors otherwise ("includes non-existent
  # path") - but these packman-fetched directories (nanovdb, slang, ...)
  # don't exist yet on a fresh checkout, only after falcor_build's custom
  # command has actually run once, at build time. A plain (non-IMPORTED)
  # target like augusta_renderer isn't checked this way, since referencing
  # not-yet-generated paths is normal for anything fed by a custom command.
  target_include_directories(augusta_renderer PRIVATE
    "${FALCOR_DIR}/Source/Falcor"
    "${FALCOR_DIR}/external/imgui"
    "${FALCOR_DIR}/external/imgui_addons"
    "${FALCOR_DIR}/external/packman/nanovdb/include"
    "${FALCOR_DIR}/external/include"
    "${FALCOR_DIR}/external/packman/rtxdi/rtxdi-sdk/include"
    "${FALCOR_DIR}/external/pybind11/include"
    "${FALCOR_DIR}/external/packman/python/Include"
    "${FALCOR_DIR}/external/packman/slang/include"
  )
  target_compile_definitions(augusta_renderer PRIVATE
    NOMINMAX UNICODE _USE_MATH_DEFINES
    FALCOR_ENABLE_ASSERTS=$<IF:$<CONFIG:Debug>,1,0>
    FALCOR_ENABLE_PROFILER=1 FALCOR_HAS_D3D12=1 FALCOR_HAS_VULKAN=0
    FALCOR_HAS_AFTERMATH=${FALCOR_HAS_AFTERMATH_VALUE} FALCOR_HAS_NVAPI=0 FALCOR_HAS_CUDA=0
    FALCOR_HAS_D3D12_AGILITY_SDK=1 FALCOR_HAS_RTXDI=1
    IMGUI_USER_CONFIG="Utils/UI/ImGuiConfig.h"
    # Source/Falcor/Utils/Math/Float16.h uses std::float_denorm_style /
    # numeric_limits::has_denorm, deprecated as of C++23 - not our header
    # to fix, and MSVC's own STL suggests this exact macro to silence it.
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
    # external/packman/nanovdb's NanoVDB.h calls plain sprintf - not our
    # header to fix either.
    _CRT_SECURE_NO_WARNINGS
    # Matches the same fix applied to Falcor's own build in
    # cmake/patches/falcor-augusta.patch: any TU that includes Falcor's
    # vendored external/fmt/include/fmt/format.h (as this target's own
    # .cpp files do) hits the same long-obsolete _SECURE_SCL/checked_ptr
    # code path in Debug config on newer MSVC otherwise.
    _SECURE_SCL=0
  )
  target_link_libraries(augusta_renderer PUBLIC
    "${FALCOR_BUILD_DIR}/Source/Falcor/${CMAKE_BUILD_TYPE}/Falcor.lib"
    # fmt's own CMakeLists applies a "d" debug postfix to its output name
    # (fmtd.lib) - unlike Falcor.lib itself, which doesn't get one.
    "${FALCOR_BUILD_DIR}/external/fmt/${CMAKE_BUILD_TYPE}/$<IF:$<CONFIG:Debug>,fmtd,fmt>.lib"
  )
  # python310.lib/slang.lib/gfx.lib live under external/packman/python and
  # external/packman/slang - directories packman itself creates as
  # links (junctions), not plain directories. target_link_libraries()
  # (and BYPRODUCTS above) register a full-path library argument as a
  # tracked Ninja *file* dependency, which makes Ninja pre-create its
  # parent directory as a plain folder before this target's own custom
  # command ever runs - colliding with packman's own attempt to link
  # that same path moments later ("already exists as a different type of
  # file"). target_link_options() emits the same literal path on the
  # link command line without Ninja tracking it as a dependency, sidestepping
  # that; add_dependencies() below still guarantees these files exist by
  # link time regardless.
  target_link_options(augusta_renderer PUBLIC
    "${FALCOR_DIR}/external/packman/python/libs/python310.lib"
    "${FALCOR_DIR}/external/packman/slang/lib/slang.lib"
    "${FALCOR_DIR}/external/packman/slang/lib/gfx.lib"
  )
  add_dependencies(augusta_renderer falcor_build)

  # augusta_math (glm) and Falcor's own fmt (external/fmt) both put an
  # `fmt/format.h` on the include path via their respective vcpkg/packman
  # installs - vcpkg's shared vcpkg_installed/.../include root (pulled in
  # transitively via augusta_input -> augusta_physics -> augusta_math)
  # happens to also carry a newer fmt than Falcor was built against, and
  # <fmt/format.h> in Falcor.h would otherwise resolve to that mismatched
  # copy. Force Falcor's own fmt to win for this target specifically.
  target_include_directories(augusta_renderer BEFORE PRIVATE "${FALCOR_DIR}/external/fmt/include")

  # Falcor's own sources build with this project-specific warning policy
  # (Source/Falcor/CMakeLists.txt); only this target's translation unit
  # (renderer.cpp) consumes Falcor's public headers, so only this target
  # needs to match. /wd5311
  # isn't part of that copied policy - it's this machine's much newer
  # MSVC/STL (Falcor's own list predates it) flagging nlohmann/json.hpp
  # and pybind11's pre-C++23 `operator""x` literal-operator syntax, in
  # vendored headers this target doesn't own either.
  target_compile_options(augusta_renderer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd4819 /wd4251 /wd4244 /wd4267 /wd4100 /wd4201 /wd4245 /wd4189 /wd4127 /wd4701 /wd4703 /wd4324 /wd4505 /wd4702 /wd4389 /wd4459 /wd4268 /wd5311>
  )
endif()

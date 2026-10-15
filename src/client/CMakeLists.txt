# The client's private libraries below use renderer.h's plain types only, so
# they take that header's directory rather than linking augusta_renderer:
# their tests need neither a GPU nor a window. renderer.h includes input.h,
# hence augusta_input.

# What the client loads from its verified pack at startup: the scene graph as
# renderer::Scene (see scene_loader.h), the characters other players bring (see
# character_loader.h), the map's collision and every cue's sound (see content.h).
add_library(augustac_content STATIC scene_loader.cpp character_loader.cpp content.cpp)
target_include_directories(
  augustac_content
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/modules/renderer/include
)
target_link_libraries(
  augustac_content
  PUBLIC augusta_assets augusta_cues augusta_failure augusta_input augusta_math augusta_physics
  PRIVATE augusta_logging augusta_map
)
augusta_set_module_scope(augustac_content CLIENT_ONLY)

# The conversions ClientRuntime makes at its edges: the harness's types into
# presentation's, and presentation's into the renderer's (see frame_mapping.h).
add_library(augustac_frame_mapping STATIC frame_mapping.cpp)
target_include_directories(
  augustac_frame_mapping
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/modules/renderer/include
)
target_link_libraries(
  augustac_frame_mapping
  PUBLIC augusta_command augusta_harness augusta_input augusta_math augusta_presentation
)
augusta_set_module_scope(augustac_frame_mapping CLIENT_ONLY)

# The connection's numbers as the debug HUD shows them and Nsight Systems plots
# them (see net_stats.h). nvtx3-cpp (third_party/nvtx, see the root
# CMakeLists.txt) is header-only.
add_library(augustac_net_stats STATIC net_stats.cpp)
target_include_directories(
  augustac_net_stats
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/modules/renderer/include
)
target_link_libraries(augustac_net_stats PUBLIC augusta_input augusta_networking nvtx3-cpp)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max in net_stats.cpp.
target_compile_definitions(augustac_net_stats PRIVATE NOMINMAX)
augusta_set_module_scope(augustac_net_stats CLIENT_ONLY)

# What the client loads in the Lobby before it reports Ready, and for which
# Roster (see lobby_readiness.h): the decision only, so its tests need no
# renderer or connection.
add_library(augustac_lobby_readiness STATIC lobby_readiness.cpp)
target_include_directories(augustac_lobby_readiness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustac_lobby_readiness PUBLIC augusta_harness)
augusta_set_module_scope(augustac_lobby_readiness CLIENT_ONLY)

# How augustac's phases classify their failures behind the application
# boundary (see application.h, ADR-0033): no window or GPU, so its tests need
# no renderer.
add_library(augustac_application STATIC application.cpp)
target_include_directories(augustac_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustac_application
  PUBLIC augustac_content augusta_assets augusta_client_config augusta_config augusta_failure augusta_harness
  PRIVATE augusta_logging augusta_networking
)
augusta_set_module_scope(augustac_application CLIENT_ONLY)

add_library(augustac_runtime STATIC runtime.cpp)
target_include_directories(augustac_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustac_runtime
  PUBLIC
    augustac_content
    augusta_assets
    augusta_failure
    augusta_harness
    augusta_input
    augusta_networking
    augusta_renderer
  # nvtx3-cpp: runtime.cpp names the Main/Render, Simulation, and Network I/O
  # thread loops for Nsight Systems.
  PRIVATE
    augustac_application
    augustac_frame_mapping
    augustac_lobby_readiness
    augustac_net_stats
    augusta_audio
    augusta_logging
    augusta_map
    augusta_physics
    augusta_prediction
    augusta_presentation
    augusta_tick
    nvtx3-cpp
)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max.
target_compile_definitions(augustac_runtime PRIVATE NOMINMAX)
augusta_set_module_scope(augustac_runtime CLIENT_ONLY)

add_executable(augustac main.cpp)
target_link_libraries(
  augustac
  PRIVATE
    augusta_assets
    augusta_client_config
    augusta_config
    augusta_failure
    augusta_lifecycle
    augusta_version
    augusta_logging
    augustac_application
    augustac_runtime
    augustac_content
)
augusta_set_module_scope(augustac CLIENT_ONLY)
# The Aftermath shared libraries beside augustac, on both platforms, when the
# build has them (cmake/Aftermath.cmake).
augusta_stage_aftermath(augustac)
# What each platform's augustac needs beyond its code: its debug info, and its
# runtime folder, which a release archives (ADR-0055, ADR-0058); and on Windows
# its timer resolution, which Linux's main.cpp has no need to raise.
if(WIN32)
  # winmm: main.cpp raises the system timer resolution (timeBeginPeriod) for the
  # life of the process, so the Prediction thread's sleeps keep its tick schedule.
  # NOMINMAX: the <windows.h> it includes for that would otherwise define min/max.
  target_link_libraries(augustac PRIVATE winmm)
  target_compile_definitions(augustac PRIVATE NOMINMAX)
  # Release writes augustac.pdb beside augustac.exe, what a released client's
  # crash or GPU crash dump is read with (ADR-0055). /DEBUG turns /OPT:REF and
  # /OPT:ICF off unless they are named, so they are: the code is Release's own.
  target_link_options(augustac PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>")

  # `cmake --install --component client` stages the client's whole runtime folder,
  # which a release zips (scripts/stage-client.sh): augustac.exe, the DLLs it
  # loads, and the MSVC runtime, so it starts on a Windows with no Visual C++
  # redistributable. CMake resolves the DLLs itself, as for augustad: vcpkg's, and
  # Aftermath's when the build has it (cmake/Aftermath.cmake). The renderer's
  # shaders are in augustac.exe, and the Vulkan loader is the system's, which
  # augustac loads at runtime rather than imports (ADR-0059).
  # EXCLUDE_FROM_ALL: a plain `cmake --install`, augustad's (Dockerfile,
  # `make install`), leaves it out.
  install(
    TARGETS augustac
    RUNTIME_DEPENDENCY_SET augustac_dlls
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(dll_directories "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
  if(AUGUSTA_AFTERMATH_DIR)
    list(APPEND dll_directories "${AUGUSTA_AFTERMATH_DIR}/lib/x64")
  endif()
  install(
    RUNTIME_DEPENDENCY_SET augustac_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\][Ss]ystem32[/\\]"
    DIRECTORIES ${dll_directories}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  include(InstallRequiredSystemLibraries)
  install(
    PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT client
    EXCLUDE_FROM_ALL
  )
else()
  # Release's debug info split off into augustac.debug, as augustad's is
  # (ADR-0047), which a release publishes beside the tarball (ADR-0055). No
  # crash handler: a player's systemd-coredump keeps the core.
  augusta_split_debug_info(augustac)
  # The shared libraries the folder bundles are found beside augustac, wherever
  # it is extracted, with no launcher script setting a library path. In the
  # build tree too, so the installed augustac is the built one, byte for byte.
  set_target_properties(augustac PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")

  # `cmake --install --component client` stages the client's runtime folder,
  # which a release tars (scripts/stage-client.sh): augustac and the shared
  # libraries it links that are not the system's. vcpkg links its ports
  # statically, and the toolchain the C++ runtime (cmake/LlvmToolchain.cmake),
  # so the folder bundles only a shared library a dependency ships alone:
  # Aftermath's when the build has it. What it links from the system is held to
  # an allowlist by the staging check.
  # EXCLUDE_FROM_ALL: a plain `cmake --install`, augustad's (Dockerfile,
  # `make install`), leaves it out.
  install(
    TARGETS augustac
    RUNTIME_DEPENDENCY_SET augustac_libraries
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
  set(library_directories "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>lib")
  if(AUGUSTA_AFTERMATH_DIR)
    list(APPEND library_directories "${AUGUSTA_AFTERMATH_DIR}/lib/x64")
  endif()
  install(
    RUNTIME_DEPENDENCY_SET augustac_libraries
    POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
    DIRECTORIES ${library_directories}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT client EXCLUDE_FROM_ALL
  )
endif()

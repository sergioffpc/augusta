# augustac and its Runtime (ADR-0060): the client Runtime and the pieces it is
# made of, each an internal target so its tests link only what it needs, then
# the executable that runs it. The pieces below use renderer.h's plain types
# only, so they take that header's directory rather than linking
# augusta_renderer: their tests need neither a GPU nor a window.
# renderer.h includes window.h, which needs no GLFW, hence
# augusta_window_events. frame_mapping.h, lobby_readiness.h and net_stats.h are
# private to the Runtime: its tests reach them through this directory, as the
# renderer's do theirs.

# What the client loads from its verified pack at startup: the scene graph as
# renderer::Scene (see client_scene_loader.h), the characters other players
# bring (see client_character_loader.h), the map's collision and every cue's
# sound (see client_content.h).
add_library(augusta_client_content STATIC client_scene_loader.cpp client_character_loader.cpp client_content.cpp)
target_include_directories(
  augusta_client_content
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src/modules/renderer/include
)
target_link_libraries(
  augusta_client_content
  PUBLIC augusta_assets augusta_cues augusta_failure augusta_math augusta_physics augusta_window_events
  PRIVATE augusta_logging
)
augusta_set_module_scope(augusta_client_content CLIENT_ONLY)

# The conversions ClientRuntime makes at its edges: the harness's types into
# presentation's, and presentation's into the renderer's (see frame_mapping.h).
add_library(augusta_client_frame_mapping STATIC frame_mapping.cpp)
target_include_directories(augusta_client_frame_mapping PUBLIC ${CMAKE_SOURCE_DIR}/src/modules/renderer/include)
target_link_libraries(
  augusta_client_frame_mapping
  PUBLIC augusta_gameplay augusta_harness augusta_input augusta_math augusta_presentation augusta_window_events
)
augusta_set_module_scope(augusta_client_frame_mapping CLIENT_ONLY)

# The connection's numbers as the debug HUD shows them and Nsight Systems plots
# them (see net_stats.h). nvtx3-cpp (third_party/nvtx, see the root
# CMakeLists.txt) is header-only.
add_library(augusta_client_net_stats STATIC net_stats.cpp)
target_link_libraries(augusta_client_net_stats PUBLIC augusta_hud augusta_link nvtx3-cpp)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max in net_stats.cpp.
target_compile_definitions(augusta_client_net_stats PRIVATE NOMINMAX)
augusta_set_module_scope(augusta_client_net_stats CLIENT_ONLY)

# What the client loads in the Lobby before it reports Ready, and for which
# Roster (see lobby_readiness.h): the decision only, so its tests need no
# renderer or connection.
add_library(augusta_client_lobby_readiness STATIC lobby_readiness.cpp)
target_link_libraries(augusta_client_lobby_readiness PUBLIC augusta_harness)
augusta_set_module_scope(augusta_client_lobby_readiness CLIENT_ONLY)

# How augustac's phases classify their failures behind the application
# boundary (see client_phases.h, ADR-0033): no window or GPU, so its tests need
# no renderer.
add_library(augusta_client_phases STATIC client_phases.cpp)
target_include_directories(augusta_client_phases PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client_phases
  PUBLIC augusta_client_content augusta_assets augusta_client_config augusta_config augusta_failure augusta_harness
  PRIVATE augusta_logging
)
augusta_set_module_scope(augusta_client_phases CLIENT_ONLY)

# The client Runtime itself (see client.h): its Main/Render thread and the
# wiring of the Harness to the renderer, the HUD, input, PresentationWorld and
# audio. It links publicly only what its header names.
add_library(augusta_client STATIC client.cpp)
target_include_directories(augusta_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client
  PUBLIC augusta_client_content augusta_assets augusta_failure augusta_harness augusta_input
  # nvtx3-cpp: client.cpp names the Main/Render, Simulation, and Network I/O
  # thread loops for Nsight Systems.
  PRIVATE
    augusta_client_phases
    augusta_client_frame_mapping
    augusta_client_lobby_readiness
    augusta_client_net_stats
    augusta_audio
    augusta_gameplay
    augusta_hud
    augusta_link
    augusta_logging
    augusta_physics
    augusta_prediction
    augusta_presentation
    augusta_renderer
    augusta_window
    nvtx3-cpp
)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max.
target_compile_definitions(augusta_client PRIVATE NOMINMAX)
augusta_set_module_scope(augusta_client CLIENT_ONLY)

# augustac's runs behind the application boundary (see client_application.h,
# ADR-0033): listing a replay server's Replays, and the Lifecycle that starts
# the transport and constructs and runs the client Runtime. What augustac's
# entry point links of the Runtime: the Runtime and the transport are linked
# privately, so it reaches neither.
add_library(augusta_client_application STATIC client_application.cpp)
target_include_directories(augusta_client_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client_application
  PUBLIC augusta_application augusta_client_config augusta_client_phases augusta_harness
  PRIVATE augusta_assets augusta_client augusta_failure augusta_link
)
augusta_set_module_scope(augusta_client_application CLIENT_ONLY)

# The files a Run leaves beside the executable (see run_files.h): their names
# from the Run ID, which Runs are kept, and how the previous one ended.
add_library(augusta_client_run_files STATIC run_files.cpp)
target_include_directories(augusta_client_run_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_client_run_files PUBLIC augusta_uuid)
augusta_set_module_scope(augusta_client_run_files CLIENT_ONLY)

# The start of one Run (see client_run.h): its Run ID, its log file and its
# first lines, before augustac reads its config.
add_library(augusta_client_run STATIC client_run.cpp)
target_include_directories(augusta_client_run PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client_run
  PUBLIC augusta_uuid
  PRIVATE augusta_client_run_files augusta_config augusta_hardware augusta_logging augusta_version
)
augusta_set_module_scope(augusta_client_run CLIENT_ONLY)

# augustac's entry point: it runs the client Runtime above behind the
# application boundary (ADR-0033, ADR-0060).
add_executable(augustac main.cpp)
target_link_libraries(
  augustac
  PRIVATE
    augusta_application
    augusta_failure
    augusta_version
    augusta_logging
    augusta_client_application
    augusta_client_run
)
augusta_set_module_scope(augustac CLIENT_ONLY)
# augustac is Glue: it links only the application, the client Runtime beside
# it, crash, failure, logging and version - anything else fails the configure
# (ADR-0060, cmake/AugustaModule.cmake).
augusta_set_executable_glue(augustac)
# The Aftermath shared libraries beside augustac, on both platforms, when the
# build has them (cmake/Aftermath.cmake).
augusta_stage_aftermath(augustac)
# What each platform's augustac needs beyond its code: its debug info, and its
# runtime folder, which a release archives (ADR-0055, ADR-0058).
if(WIN32)
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

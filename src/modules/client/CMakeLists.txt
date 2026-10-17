# The client Runtime, augustac's (ADR-0060), and the pieces it is made of, each
# an internal target so its tests link only what it needs. The pieces below use
# renderer.h's plain types only, so they take that header's directory rather
# than linking augusta_renderer: their tests need neither a GPU nor a window.
# renderer.h includes window.h, which needs no GLFW, hence
# augusta_window_events. frame_mapping.h, lobby_readiness.h and net_stats.h are
# private to the module: its tests reach them through this directory, as the
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
# boundary (see client_application.h, ADR-0033): no window or GPU, so its tests
# need no renderer.
add_library(augusta_client_application STATIC client_application.cpp)
target_include_directories(augusta_client_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client_application
  PUBLIC augusta_client_content augusta_assets augusta_client_config augusta_config augusta_failure augusta_harness
  PRIVATE augusta_logging
)
augusta_set_module_scope(augusta_client_application CLIENT_ONLY)

# The client Runtime itself (see client.h): its Main/Render thread and the
# wiring of the Harness to the renderer, the HUD, input, PresentationWorld and
# audio.
add_library(augusta_client STATIC client.cpp)
target_include_directories(augusta_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_client
  PUBLIC
    augusta_client_content
    augusta_assets
    augusta_failure
    augusta_harness
    augusta_hud
    augusta_input
    # augustac's link::Init, once per process.
    augusta_link
    augusta_renderer
    augusta_window
  # nvtx3-cpp: client.cpp names the Main/Render, Simulation, and Network I/O
  # thread loops for Nsight Systems.
  PRIVATE
    augusta_client_application
    augusta_client_frame_mapping
    augusta_client_lobby_readiness
    augusta_client_net_stats
    augusta_audio
    augusta_gameplay
    augusta_logging
    augusta_physics
    augusta_prediction
    augusta_presentation
    nvtx3-cpp
)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max.
target_compile_definitions(augusta_client PRIVATE NOMINMAX)
augusta_set_module_scope(augusta_client CLIENT_ONLY)

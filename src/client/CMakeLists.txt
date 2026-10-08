# The client's private libraries below use renderer.h's plain types only, so
# they take that header's directory rather than linking augusta_renderer: their
# tests need neither Falcor nor a window. renderer.h includes input.h, hence
# augusta_input.

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
  PUBLIC augusta_assets augusta_cues augusta_input augusta_math augusta_physics
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
    augusta_application
    augusta_client_config
    augusta_config
    augusta_failure
    augusta_version
    augusta_logging
    augustac_application
    augustac_runtime
    augustac_content
)
augusta_set_module_scope(augustac CLIENT_ONLY)
# winmm: main.cpp raises the system timer resolution (timeBeginPeriod) for the
# life of the process, so the Prediction thread's sleeps keep its tick schedule.
# NOMINMAX: the <windows.h> it includes for that would otherwise define min/max.
target_link_libraries(augustac PRIVATE winmm)
target_compile_definitions(augustac PRIVATE NOMINMAX)

# Falcor.dll (ADR-0009) and its own runtime dependencies (D3D12 Agility SDK,
# dxcompiler, ...) and shader library need to sit next to augustac.exe -
# FALCOR_BIN_DIR is set by src/modules/renderer/CMakeLists.txt, the only
# other consumer of the vendored Falcor build.
add_custom_command(
  TARGET augustac
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FALCOR_BIN_DIR}" "$<TARGET_FILE_DIR:augustac>"
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/modules/renderer/shaders"
    "$<TARGET_FILE_DIR:augustac>/shaders/Augusta/Renderer"
  COMMENT "Deploying Falcor's runtime (ADR-0009) and shaders next to augustac"
)

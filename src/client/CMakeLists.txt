# Cooked scene graph -> renderer::Scene. Split from augustac_runtime so its
# tests need neither Falcor nor a window: it only uses renderer.h's plain
# scene types, so it takes that header's directory rather than linking
# augusta_renderer.
add_library(augustac_scene STATIC scene_loader.cpp)
target_include_directories(augustac_scene PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/modules/renderer/include
)
target_link_libraries(augustac_scene PUBLIC augusta_assets augusta_input augusta_math)
augusta_set_module_scope(augustac_scene CLIENT_ONLY)

add_library(augustac_runtime STATIC
  runtime.cpp
)
target_include_directories(augustac_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustac_runtime
  PUBLIC augusta_assets augusta_audio augusta_harness augusta_input augusta_networking augusta_physics augusta_prediction augusta_presentation augusta_renderer
  # nvtx3-cpp (third_party/nvtx, see the root CMakeLists.txt) is
  # header-only - runtime.cpp uses it to name the Main/Render,
  # Simulation, and Network I/O thread loops for Nsight Systems.
  PRIVATE augusta_logging augusta_tick nvtx3-cpp
)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max in runtime.cpp.
target_compile_definitions(augustac_runtime PRIVATE NOMINMAX)
augusta_set_module_scope(augustac_runtime CLIENT_ONLY)

add_executable(augustac main.cpp)
target_link_libraries(augustac PRIVATE augusta_assets augusta_config augusta_map augusta_version augusta_logging augusta_networking augustac_runtime augustac_scene)
# winmm: main.cpp raises the system timer resolution (timeBeginPeriod) for the
# life of the process, so the Prediction thread's sleeps keep its tick schedule.
# NOMINMAX: the <windows.h> it includes for that would otherwise define min/max.
target_link_libraries(augustac PRIVATE winmm)
target_compile_definitions(augustac PRIVATE NOMINMAX)

# Falcor.dll (ADR-0009) and its own runtime dependencies (D3D12 Agility SDK,
# dxcompiler, ...) and shader library need to sit next to augustac.exe -
# FALCOR_BIN_DIR is set by src/modules/renderer/CMakeLists.txt, the only
# other consumer of the vendored Falcor build.
add_custom_command(TARGET augustac POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FALCOR_BIN_DIR}" "$<TARGET_FILE_DIR:augustac>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:augustac>/shaders/Augusta/Renderer"
  COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/src/modules/renderer/shaders/Renderer.3d.slang"
    "$<TARGET_FILE_DIR:augustac>/shaders/Augusta/Renderer/Renderer.3d.slang"
  COMMENT "Deploying Falcor's runtime (ADR-0009) and shaders next to augustac"
)

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
  PUBLIC augusta_audio augusta_input augusta_networking augusta_physics augusta_prediction augusta_presentation augusta_renderer
  # nvtx3-cpp (third_party/nvtx, see the root CMakeLists.txt) is
  # header-only - runtime.cpp uses it to name the Main/Render,
  # Simulation, and Network I/O thread loops for Nsight Systems.
  PRIVATE augusta_assets augusta_logging nvtx3-cpp
)
# NOMINMAX: something in this TU's includes pulls in <windows.h>, whose
# min/max macros would break std::min/std::max in runtime.cpp.
target_compile_definitions(augustac_runtime PRIVATE NOMINMAX)
augusta_set_module_scope(augustac_runtime CLIENT_ONLY)

add_executable(augustac main.cpp)
target_link_libraries(augustac PRIVATE augusta_assets augusta_version augusta_logging augusta_networking augustac_runtime augustac_scene)

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

# PhysX's GPU acceleration module (PhysXGpu_64.dll/PhysXDevice64.dll) is
# late-bound - PxCreateCudaContextManager (physics.cpp) LoadLibrary()s it
# by name at runtime rather than linking against it, so without this copy
# augusta_physics::World's own GPU request (client only - see
# augusta::prediction::World's constructor) always silently falls back to
# CPU, even on a machine with a capable GPU/driver. Client-only
# (src/client is only added under the WIN32 guard above; the server never
# requests GPU physics, see physics::World's own header comment).
add_custom_command(TARGET augustac POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:unofficial::omniverse-physx-sdk::gpu-library>"
    "$<TARGET_FILE:unofficial::omniverse-physx-sdk::gpu-device-library>"
    "$<TARGET_FILE_DIR:augustac>"
  COMMENT "Deploying PhysX's GPU acceleration runtime next to augustac"
)

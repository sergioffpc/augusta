add_library(augustac_runtime STATIC
  runtime.cpp
)
target_include_directories(augustac_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustac_runtime
  PUBLIC augusta_audio augusta_input augusta_networking augusta_physics augusta_prediction augusta_presentation augusta_renderer
  PRIVATE augusta_logging
)

add_executable(augustac main.cpp)
target_link_libraries(augustac PRIVATE augusta_version augusta_logging augusta_networking augustac_runtime)

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

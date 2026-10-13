# The geometry the Renderer builds on the CPU (vertices.h), free of any GPU API
# so it is built and tested on every platform. Private to the module: its header
# is reached through this directory, as frame_regions.h is.
add_library(augusta_renderer_vertices STATIC vertices.cpp)
target_include_directories(augusta_renderer_vertices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer_vertices PUBLIC augusta_input)
augusta_set_module_scope(augusta_renderer_vertices CLIENT_ONLY)

# The debug HUD's text (debug_hud_text.h), which the Renderer draws. Free of
# any GPU API, like the geometry above, and private to the module the same way.
add_library(augusta_renderer_debug_hud_text STATIC debug_hud_text.cpp)
target_include_directories(augusta_renderer_debug_hud_text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer_debug_hud_text PUBLIC augusta_input)
augusta_set_module_scope(augusta_renderer_debug_hud_text CLIENT_ONLY)

if(WIN32)
  include(${CMAKE_SOURCE_DIR}/cmake/Aftermath.cmake)

  # The Renderer on NVRHI and GLFW (ADR-0059), which augustac links and
  # tests/client_renderer_smoke_test.cpp drives. NVRHI's backend is D3D12 alone,
  # and the manifest has both, ImGui and the shader compilers below on Windows
  # only.
  find_package(nvrhi CONFIG REQUIRED)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(imgui CONFIG REQUIRED)

  # Which input::Key a GLFW key is (glfw_keys.h), apart from the window so it is
  # tested without one. Private to the module, as vertices.h is.
  add_library(augusta_renderer_glfw_keys STATIC glfw_keys.cpp)
  target_link_libraries(augusta_renderer_glfw_keys PUBLIC augusta_input PRIVATE glfw)
  augusta_set_module_scope(augusta_renderer_glfw_keys CLIENT_ONLY)

  # Its shaders, compiled to DXIL at build time and embedded in the renderer as
  # byte arrays: it compiles nothing at runtime and ships no shader files, and a
  # shader error fails the build. slangc hands its HLSL to DXC; the manifest has
  # both as host tools, which nothing ships.
  find_package(slang CONFIG REQUIRED)
  find_package(directx-dxc CONFIG REQUIRED)
  cmake_path(GET DIRECTX_DXC_TOOL PARENT_PATH dxc_dir)
  set(shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(shader_headers "")
  # Compiles the entry point entry of shaders/source, for stage, into
  # <name>.h: the DXIL as the byte array name.
  function(augusta_compile_shader source entry stage name)
    set(output ${shader_dir}/${name}.h)
    add_custom_command(
      OUTPUT ${output}
      COMMAND
        ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source} -target dxil -profile sm_6_0 -entry ${entry}
        -stage ${stage} -matrix-layout-column-major -warnings-as-errors all -dxc-path ${dxc_dir} -o ${output}
        -source-embed-style u8 -source-embed-name ${name}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}
      COMMENT "Compiling ${source}'s ${entry} to DXIL"
      VERBATIM
    )
    set(shader_headers ${shader_headers} ${output} PARENT_SCOPE)
  endfunction()
  augusta_compile_shader(ImGui.slang vsMain vertex kImGuiVs)
  augusta_compile_shader(ImGui.slang psMain fragment kImGuiPs)
  augusta_compile_shader(Renderer.3d.slang vsMain vertex kSceneVs)
  augusta_compile_shader(Renderer.3d.slang psMain fragment kScenePs)
  augusta_compile_shader(Effects.3d.slang vsMain vertex kEffectsVs)
  augusta_compile_shader(Effects.3d.slang psMain fragment kEffectsPs)

  # The debug HUD's monospace font (fonts/, with its licence), embedded the same
  # way, so there is no font file to ship or find.
  set(font_header ${shader_dir}/debug_hud_font.h)
  set(font ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-Bold.ttf)
  add_custom_command(
    OUTPUT ${font_header}
    COMMAND
      ${CMAKE_COMMAND} -DINPUT=${font} -DOUTPUT=${font_header} -DNAME=kDebugHudFont -P
      ${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${font} ${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding the debug HUD's font"
    VERBATIM
  )

  add_library(
    augusta_renderer
    STATIC
    renderer.cpp
    debug_hud.cpp
    imgui_renderer.cpp
    gpu_crash_dumps.cpp
    ${shader_headers}
    ${font_header}
  )
  target_include_directories(augusta_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${shader_dir})
  target_link_libraries(
    augusta_renderer
    PUBLIC augusta_input
    PRIVATE
      augusta_logging
      augusta_renderer_glfw_keys
      augusta_renderer_debug_hud_text
      augusta_renderer_vertices
      nvrhi
      nvrhi_d3d12
      glfw
      imgui::imgui
      d3d12
      dxgi
  )
  # NOMINMAX: renderer.cpp includes <windows.h>, whose min/max macros would
  # break std::numeric_limits<>::max().
  target_compile_definitions(augusta_renderer PRIVATE NOMINMAX)
  augusta_set_module_scope(augusta_renderer CLIENT_ONLY)

  # GPU crash dumps (gpu_crash_dumps.h) with Nsight Aftermath when its SDK is
  # found (cmake/Aftermath.cmake), and none without. vcpkg's NVRHI is built
  # without its own Aftermath support, so the Renderer attaches Aftermath to
  # the D3D12 device itself. An executable linking this target stages the
  # Aftermath DLLs beside it with augusta_stage_aftermath.
  if(AUGUSTA_AFTERMATH_DIR)
    target_link_libraries(augusta_renderer PRIVATE augusta_aftermath augusta_version)
    target_compile_definitions(augusta_renderer PRIVATE AUGUSTA_HAS_AFTERMATH=1)
  else()
    target_compile_definitions(augusta_renderer PRIVATE AUGUSTA_HAS_AFTERMATH=0)
  endif()
endif()

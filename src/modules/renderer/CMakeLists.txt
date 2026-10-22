# The geometry the Renderer builds on the CPU (vertices.h), free of any GPU API
# so it is built and tested on every platform. Private to the module: its header
# is reached through this directory, as frame_regions.h is.
add_library(augusta_renderer_vertices STATIC vertices.cpp)
target_include_directories(augusta_renderer_vertices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer_vertices PUBLIC augusta_math augusta_window_events)
augusta_set_module_scope(augusta_renderer_vertices CLIENT_ONLY)

# Which Vulkan device the Renderer draws with (vulkan_device_choice.h), decided apart
# from the GPU API, and private to the module the same way.
add_library(augusta_renderer_vulkan_device_choice STATIC vulkan_device_choice.cpp)
target_include_directories(augusta_renderer_vulkan_device_choice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer_vulkan_device_choice PUBLIC augusta_math augusta_window_events)
augusta_set_module_scope(augusta_renderer_vulkan_device_choice CLIENT_ONLY)

# renderer.h, which both of the above reach, names augusta_debug's shapes in a
# Development build (ADR-0075).
if(AUGUSTA_DEBUG_TOOLS)
  target_link_libraries(augusta_renderer_vertices PUBLIC augusta_debug)
  target_link_libraries(augusta_renderer_vulkan_device_choice PUBLIC augusta_debug)
endif()

# The files GPU crash dumps are written to (gpu_crash_dump_files.h), apart from
# Aftermath so it is built and tested on every platform. Public, unlike the two
# above: the client names the files (client_run.h).
add_library(augusta_renderer_gpu_crash_dump_files STATIC gpu_crash_dump_files.cpp)
target_include_directories(augusta_renderer_gpu_crash_dump_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_renderer_gpu_crash_dump_files CLIENT_ONLY)

if(AUGUSTA_CLIENT)
  # The Renderer on NVRHI's Vulkan backend (ADR-0059), which augustac links and
  # tests/client_renderer_smoke_test.cpp drives. NVRHI, ImGui and the shader
  # compiler below are the manifest's client feature, which AUGUSTA_CLIENT
  # installs.
  find_package(nvrhi CONFIG REQUIRED)
  find_package(VulkanHeaders CONFIG REQUIRED)

  # Its shaders, compiled to SPIR-V at build time and embedded in the renderer
  # as arrays of 32-bit words: it compiles nothing at runtime and ships no
  # shader files, and a shader error fails the build. slangc is a host tool in
  # the manifest, which nothing ships. Registers move to the Vulkan bindings
  # NVRHI's binding layouts give them by default (nvrhi::VulkanBindingOffsets).
  find_package(slang CONFIG REQUIRED)
  set(shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(shader_headers "")
  # Compiles the entry point entry of shaders/source, for stage, into
  # <name>.h: the SPIR-V as the array name.
  function(augusta_compile_shader source entry stage name)
    set(output ${shader_dir}/${name}.h)
    add_custom_command(
      OUTPUT ${output}
      COMMAND
        ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source} -target spirv -entry ${entry} -stage ${stage}
        -matrix-layout-column-major -fvk-t-shift 0 0 -fvk-s-shift 128 0 -fvk-b-shift 256 0 -fvk-u-shift 384 0
        -warnings-as-errors all -o ${output} -source-embed-style u32 -source-embed-name ${name}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}
      COMMENT "Compiling ${source}'s ${entry} to SPIR-V"
      VERBATIM
    )
    set(shader_headers ${shader_headers} ${output} PARENT_SCOPE)
  endfunction()
  augusta_compile_shader(Renderer.3d.slang vsMain vertex kSceneVs)
  augusta_compile_shader(Renderer.3d.slang psMain fragment kScenePs)
  augusta_compile_shader(Effects.3d.slang vsMain vertex kEffectsVs)
  augusta_compile_shader(Effects.3d.slang psMain fragment kEffectsPs)

  add_library(augusta_renderer STATIC renderer.cpp vulkan_device.cpp vulkan_swapchain.cpp vulkan_gpu_crash_dumps.cpp)
  target_include_directories(augusta_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${shader_dir})
  # No Vulkan loader is linked: Vulkan is reached through the system's, which
  # the window finds at runtime, so the client ships none (ADR-0059).
  target_link_libraries(
    augusta_renderer
    PUBLIC augusta_math augusta_window
    PRIVATE
      augusta_logging
      augusta_renderer_vulkan_device_choice
      augusta_renderer_vertices
      nvrhi
      nvrhi_vk
      Vulkan::Headers
  )
  # Vulkan-Hpp calls Vulkan through a dispatcher filled in at runtime, as NVRHI
  # does (vulkan_device.cpp). NOMINMAX: on Windows, NVRHI's Vulkan target has
  # vulkan.h include <windows.h>, whose min/max macros would break
  # std::numeric_limits<>::max().
  target_compile_definitions(augusta_renderer PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 NOMINMAX)
  augusta_set_module_scope(augusta_renderer CLIENT_ONLY)

  # The debug HUD (debug_hud.h), drawn with Dear ImGui, and the Debug overlays'
  # shapes (debug_shapes_pass.h), which augusta_debug describes, public in
  # renderer.h, in a Development build only (ADR-0074, ADR-0075): a Shipping
  # build compiles no HUD or shapes pass, no ImGui or shapes shader, no font and
  # no augusta_debug.
  if(AUGUSTA_DEBUG_TOOLS)
    augusta_compile_shader(DebugShapes.3d.slang vsMain vertex kDebugShapesVs)
    augusta_compile_shader(DebugShapes.3d.slang psMain fragment kDebugShapesPs)
    target_sources(augusta_renderer PRIVATE debug_shapes_pass.cpp)
    target_link_libraries(augusta_renderer PUBLIC augusta_debug)

    find_package(imgui CONFIG REQUIRED)
    augusta_compile_shader(ImGui.slang vsMain vertex kImGuiVs)
    augusta_compile_shader(ImGui.slang psMain fragment kImGuiPs)

    # The debug HUD's monospace font (fonts/, with its licence), embedded as the
    # shaders are, so there is no font file to ship or find.
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
    list(APPEND shader_headers ${font_header})
    target_sources(augusta_renderer PRIVATE debug_hud.cpp imgui_renderer.cpp)
    target_link_libraries(augusta_renderer PRIVATE imgui::imgui)
  endif()
  target_sources(augusta_renderer PRIVATE ${shader_headers})

  # GPU crash dumps (vulkan_gpu_crash_dumps.h) with Nsight Aftermath when its SDK is
  # found (cmake/Aftermath.cmake), and none without. vcpkg's NVRHI is built
  # without its own Aftermath support, so the Renderer attaches Aftermath to
  # the Vulkan device itself. An executable linking this target stages the
  # Aftermath shared libraries beside it with augusta_stage_aftermath.
  include(${CMAKE_SOURCE_DIR}/cmake/Aftermath.cmake)
  if(AUGUSTA_AFTERMATH_DIR)
    find_package(boost_dll CONFIG REQUIRED)
    target_link_libraries(
      augusta_renderer
      PRIVATE augusta_aftermath augusta_renderer_gpu_crash_dump_files augusta_version Boost::dll
    )
    target_compile_definitions(augusta_renderer PRIVATE AUGUSTA_HAS_AFTERMATH=1)
  else()
    target_compile_definitions(augusta_renderer PRIVATE AUGUSTA_HAS_AFTERMATH=0)
  endif()
endif()

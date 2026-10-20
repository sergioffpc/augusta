# The window's device events (window_events.h), free of GLFW: input reads them
# on every platform, even where the client and GLFW are not built.
add_library(augusta_window_events STATIC events.cpp)
target_include_directories(augusta_window_events PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_window_events CLIENT_ONLY)

if(AUGUSTA_CLIENT)
  # The window (ADR-0059), which the client Runtime owns. GLFW, the
  # manifest's client feature, which AUGUSTA_CLIENT installs, is private to the
  # module, as are glfw_keys.h, its mapping of GLFW's keys, and internals.h, its
  # GLFW window: their tests reach them through this directory. Only Vulkan's
  # handle types, for the surface, are public, in window_vulkan.h.
  find_package(glfw3 CONFIG REQUIRED)
  find_package(VulkanHeaders CONFIG REQUIRED)
  add_library(augusta_window STATIC window.cpp glfw_keys.cpp)
  target_include_directories(augusta_window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(augusta_window PUBLIC augusta_window_events Vulkan::Headers PRIVATE glfw)
  # GLFW declares its Vulkan functions (window_vulkan.h's) only with Vulkan's
  # header, which this has it include.
  target_compile_definitions(augusta_window PRIVATE GLFW_INCLUDE_VULKAN)
  augusta_set_module_scope(augusta_window CLIENT_ONLY)
endif()

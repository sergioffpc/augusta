# The vocabulary of Debug overlays and Debug panels (ADR-0075), free of any GPU
# API, of ImGui and of every area module: built and tested on every platform, in
# a Development build only (ADR-0074). The Renderer draws what it describes, and
# each area's module builds it from its own state.
add_library(augusta_debug STATIC kept_shapes.cpp shapes.cpp)
target_include_directories(augusta_debug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_debug PUBLIC augusta_math augusta_window_events)
augusta_set_module_scope(augusta_debug CLIENT_ONLY)

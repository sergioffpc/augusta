# What the DebugHud shows, as text lines (ADR-0059), free of any GPU API: built
# and tested on every platform.
add_library(augusta_hud STATIC hud.cpp)
target_include_directories(augusta_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_hud CLIENT_ONLY)

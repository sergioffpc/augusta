add_library(augusta_input STATIC
  input.cpp
)
target_include_directories(augusta_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_input PUBLIC augusta_command augusta_math PRIVATE augusta_physics)
# Shared, not client-only despite ARCHITECTURE.md §5's "Input handling" - only
# because augusta_config (shared) names the keys and controls the client's
# config file binds. The Command it samples is augusta_command's, so nothing
# server-side needs this module for it.
augusta_set_module_scope(augusta_input SHARED)

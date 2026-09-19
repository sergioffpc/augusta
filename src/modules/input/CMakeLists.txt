add_library(augusta_input STATIC
  input.cpp
)
target_include_directories(augusta_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_input PUBLIC augusta_physics)
# Shared, not client-only despite ARCHITECTURE.md §5's simplified prose
# labeling ("Input handling") - Command's layout is also needed to
# deserialize what a client sent, server-side (see augusta/input.h's own
# comment on Command).
augusta_set_module_scope(augusta_input SHARED)

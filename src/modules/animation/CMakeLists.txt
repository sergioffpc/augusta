# A Character's Skeleton posed (ADR-0061): its Hitbox pose evaluated into each
# Joint's world transform; see animation.h. Shared, below presentation, on
# physics and gameplay alone; its header names their types and assets' Skeleton,
# which gameplay's headers already carry, hence PUBLIC.
add_library(augusta_animation STATIC animation.cpp)
target_include_directories(augusta_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_animation PUBLIC augusta_gameplay augusta_physics)
augusta_set_module_scope(augusta_animation SHARED)

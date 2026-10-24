# A Character's Skeleton posed (ADR-0061): its Hitbox pose evaluated into each
# Joint's world transform; see animation.h. And its Clips, loaded from its pack
# with ozz-animation's runtime (ADR-0066); see clips.h. Shared, below
# presentation, and allowed physics and gameplay at most; it needs gameplay,
# whose headers carry the physics and assets types its own headers name, and
# ozz-animation's runtime, whose types clips.h holds, hence PUBLIC.
add_library(augusta_animation STATIC animation.cpp clips.cpp)
target_include_directories(augusta_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_animation PUBLIC augusta_gameplay ozz::animation)
augusta_set_module_scope(augusta_animation SHARED)

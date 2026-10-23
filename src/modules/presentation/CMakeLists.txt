# The client's PresentationWorld and its phases (ADR-0024), the animation
# engine behind its Animation phase among them, turning interpolated
# movement and weapon state into what the client shows and plays
# (see presentation.h and animation.h).
add_library(
  augusta_presentation
  STATIC
  animation.cpp
  audio_cues.cpp
  correction.cpp
  effects.cpp
  interpolation.cpp
  local_view.cpp
  presentation.cpp
  tracers.cpp
)
target_include_directories(augusta_presentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_presentation
  PUBLIC augusta_prediction augusta_physics augusta_audio augusta_gameplay augusta_cues
  PRIVATE nvtx3-cpp $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
augusta_set_module_scope(augusta_presentation CLIENT_ONLY)
# The Physics Debug overlays and their panel (see physics_overlays.h), Shot
# judgements' rounds (judged_rounds.h) and the Hitboxes both outline
# (shown_hitboxes.h), in a Development build only (ADR-0074, ADR-0075): the
# one place presentation depends on augusta_debug.
if(AUGUSTA_DEBUG_TOOLS)
  target_sources(augusta_presentation PRIVATE judged_rounds.cpp physics_overlays.cpp shown_hitboxes.cpp)
  target_link_libraries(augusta_presentation PUBLIC augusta_debug)
endif()

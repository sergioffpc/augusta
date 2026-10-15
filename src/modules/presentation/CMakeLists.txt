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
  PUBLIC augusta_prediction augusta_physics augusta_audio augusta_ballistics augusta_cues augusta_tick
  PRIVATE nvtx3-cpp $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
augusta_set_module_scope(augusta_presentation CLIENT_ONLY)

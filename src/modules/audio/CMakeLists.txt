# The client's cue catalogue and the loading of each cue's sound from the
# client pack (ADR-0020). Its own target, apart from the audio engine's output
# device and spatialization, so loading the sounds is tested on every platform.
add_library(augusta_cues STATIC
  cues.cpp
)
target_include_directories(augusta_cues PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_cues PUBLIC augusta_assets)
augusta_set_module_scope(augusta_cues CLIENT_ONLY)

add_library(augusta_audio STATIC
  audio.cpp
)
target_include_directories(augusta_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_audio PUBLIC augusta_math)
augusta_set_module_scope(augusta_audio CLIENT_ONLY)

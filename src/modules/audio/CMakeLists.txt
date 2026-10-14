# The client's cue catalogue and the loading of each cue's sound from the
# client pack (ADR-0020). Its own target, apart from the audio engine's output
# device and spatialization, so loading the sounds is tested on every platform.
add_library(augusta_cues STATIC cues.cpp)
target_include_directories(augusta_cues PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_cues PUBLIC augusta_assets)
augusta_set_module_scope(augusta_cues CLIENT_ONLY)

add_library(augusta_audio STATIC audio.cpp)
target_include_directories(augusta_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_audio PUBLIC augusta_assets augusta_math PRIVATE augusta_logging)
augusta_set_module_scope(augusta_audio CLIENT_ONLY)

# The output device, through miniaudio and Steam Audio (ADR-0010, ADR-0028), on
# whenever the client is built, on Windows and Linux: both libraries are the
# manifest's client feature, which AUGUSTA_CLIENT installs. A build without the
# client - the server image's, and CI's Linux jobs', whose tests link this
# module through augusta_presentation - has an Engine that is always silent,
# and no audio dependency. augustad links none of it either way.
option(AUGUSTA_AUDIO_OUTPUT "Play sound through miniaudio and Steam Audio" ${AUGUSTA_CLIENT})
if(AUGUSTA_AUDIO_OUTPUT)
  # Linux's Steam Audio is a static library, whose CMake config names the
  # libraries it links but finds none of them: found first, as it would.
  if(NOT WIN32)
    find_package(flatbuffers CONFIG REQUIRED)
    find_package(pffft CONFIG REQUIRED)
    find_package(mysofa CONFIG REQUIRED)
  endif()
  find_package(steam-audio CONFIG REQUIRED)
  find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
  target_sources(augusta_audio PRIVATE miniaudio.cpp output_miniaudio.cpp)
  target_include_directories(augusta_audio SYSTEM PRIVATE ${MINIAUDIO_INCLUDE_DIR})
  target_link_libraries(augusta_audio PRIVATE unofficial::steam-audio::phonon)
  # Only playback and PCM conversion: no decoders, encoders, node graph or
  # engine. No null backend either, so a machine without an output device fails
  # to open one, and Engine says so, rather than playing to nothing unnoticed.
  target_compile_definitions(
    augusta_audio
    PRIVATE
      MA_NO_DECODING
      MA_NO_ENCODING
      MA_NO_GENERATION
      MA_NO_RESOURCE_MANAGER
      MA_NO_NODE_GRAPH
      MA_NO_ENGINE
      MA_NO_NULL
  )
else()
  target_sources(augusta_audio PRIVATE output_none.cpp)
endif()

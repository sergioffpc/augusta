add_library(augusta_audio STATIC
  audio.cpp
)
target_include_directories(augusta_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

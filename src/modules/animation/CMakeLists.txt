add_library(augusta_animation STATIC
  animation.cpp
)
target_include_directories(augusta_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_animation PUBLIC augusta_physics)

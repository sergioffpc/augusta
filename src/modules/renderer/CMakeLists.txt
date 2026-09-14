add_library(augusta_renderer STATIC
  renderer.cpp
)
target_include_directories(augusta_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_renderer PUBLIC augusta_input)

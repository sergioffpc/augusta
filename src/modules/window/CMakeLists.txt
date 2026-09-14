add_library(augusta_window STATIC
  window.cpp
)
target_include_directories(augusta_window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

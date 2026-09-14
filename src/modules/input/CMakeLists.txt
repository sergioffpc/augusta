add_library(augusta_input STATIC
  input.cpp
)
target_include_directories(augusta_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

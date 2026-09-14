add_library(augusta_networking STATIC
  networking.cpp
)
target_include_directories(augusta_networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(augusta_ballistics STATIC
  ballistics.cpp
)
target_include_directories(augusta_ballistics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(augusta_assets STATIC
  assets.cpp
)
target_include_directories(augusta_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_assets PUBLIC augusta_math)

add_library(augusta_grid STATIC
  grid.cpp
)
target_include_directories(augusta_grid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_grid PUBLIC augusta_math)
augusta_set_module_scope(augusta_grid SHARED)

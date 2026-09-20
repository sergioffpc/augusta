add_library(augusta_map STATIC
  map.cpp
)
target_include_directories(augusta_map PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_map PUBLIC augusta_assets augusta_physics augusta_math)
augusta_set_module_scope(augusta_map SHARED)

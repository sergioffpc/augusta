add_library(augusta_level STATIC
  level.cpp
)
target_include_directories(augusta_level PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_level PUBLIC augusta_assets augusta_physics augusta_math)
augusta_set_module_scope(augusta_level SHARED)

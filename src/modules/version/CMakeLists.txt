add_library(augusta_version STATIC
  version.cpp
)
target_include_directories(augusta_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_version SHARED)

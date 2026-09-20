add_library(augusta_protocol STATIC
  protocol.cpp
)
target_include_directories(augusta_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_protocol SHARED)

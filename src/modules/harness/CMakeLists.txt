add_library(augusta_harness STATIC
  harness.cpp
)
target_include_directories(augusta_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_harness
  PUBLIC augusta_networking augusta_prediction augusta_input augusta_physics augusta_protocol augusta_version
  PRIVATE augusta_logging
)
augusta_set_module_scope(augusta_harness CLIENT_ONLY)

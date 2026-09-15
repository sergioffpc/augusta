add_library(augustac_runtime STATIC
  runtime.cpp
)
target_include_directories(augustac_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustac_runtime
  PUBLIC augusta_audio augusta_input augusta_networking augusta_physics augusta_prediction augusta_presentation augusta_renderer
  PRIVATE augusta_logging
)

add_executable(augustac main.cpp)
target_link_libraries(augustac PRIVATE augusta_version augusta_logging augusta_networking augustac_runtime)

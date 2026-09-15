add_library(augustad_runtime STATIC
  runtime.cpp
)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_runtime
  PUBLIC augusta_networking augusta_physics augusta_simulation
  PRIVATE augusta_input augusta_logging
)

add_executable(augustad main.cpp)
target_link_libraries(augustad PRIVATE augusta_version augusta_logging augusta_networking augustad_runtime)

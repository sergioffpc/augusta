add_library(augusta_shared STATIC
  engine_version.cpp
)
target_include_directories(augusta_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_shared PUBLIC spdlog::spdlog)

add_library(augusta_scripting STATIC
  scripting.cpp
)
target_include_directories(augusta_scripting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

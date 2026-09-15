add_library(augusta_replication STATIC
  replication.cpp
)
target_include_directories(augusta_replication PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

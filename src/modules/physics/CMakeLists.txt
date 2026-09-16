add_library(augusta_physics STATIC
  physics.cpp
)
target_include_directories(augusta_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_physics
  PUBLIC augusta_math
  PRIVATE augusta_logging unofficial::omniverse-physx-sdk::sdk
)

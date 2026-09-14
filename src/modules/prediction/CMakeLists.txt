add_library(augusta_prediction STATIC
  prediction.cpp
)
target_include_directories(augusta_prediction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_prediction
  PUBLIC augusta_physics augusta_input
  PRIVATE $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

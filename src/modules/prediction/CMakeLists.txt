add_library(augusta_prediction STATIC
  prediction.cpp
  reconciliation.cpp
)
target_include_directories(augusta_prediction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_prediction
  PUBLIC augusta_physics augusta_input
  PRIVATE augusta_logging nvtx3-cpp $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
augusta_set_module_scope(augusta_prediction CLIENT_ONLY)

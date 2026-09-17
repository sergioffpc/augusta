add_library(augusta_simulation STATIC
  simulation.cpp
)
target_include_directories(augusta_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_simulation
  PUBLIC augusta_physics augusta_ballistics augusta_scripting augusta_input
  PRIVATE augusta_logging $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
augusta_set_module_scope(augusta_simulation SERVER_ONLY)

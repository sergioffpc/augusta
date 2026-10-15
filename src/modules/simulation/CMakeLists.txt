# The server's authoritative World (SimulationWorld, ADR-0023) and what each
# client is sent of it (augusta::replication, ADR-0044; see replication.h).
add_library(augusta_simulation STATIC hitbox_history.cpp policy_actions.cpp replication.cpp simulation.cpp)
target_include_directories(augusta_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_simulation
  PUBLIC augusta_physics augusta_gameplay augusta_scripting
  PRIVATE augusta_logging $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
augusta_set_module_scope(augusta_simulation SERVER_ONLY)

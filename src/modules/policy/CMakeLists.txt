# Game policy (ADR-0022): the rules' hooks, the views built for them and the
# typed Policy actions read from their answers, which SimulationWorld and the
# scenario loader call. Over the sandboxed Lua embedding (augusta_scripting),
# which no header of its own names, so its callers never link it. The Tick in a
# view's on_tick is the simulation's time (augusta/tick.h).
add_library(augusta_policy STATIC answers.cpp policy.cpp)
target_include_directories(augusta_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_policy PUBLIC augusta_tick augusta_uuid PRIVATE augusta_scripting)
augusta_set_module_scope(augusta_policy SERVER_ONLY)

# Reads the scenario's Game policy scripts out of the server pack into the
# Game policy SimulationWorld runs (ADR-0022); tested with packs built in the
# test (see augusta/policy_loader.h). A target of its own, so SimulationWorld,
# which runs the policy, never links the pack.
add_library(augusta_policy_loader STATIC policy_loader.cpp)
target_include_directories(augusta_policy_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_policy_loader PUBLIC augusta_assets augusta_policy)
augusta_set_module_scope(augusta_policy_loader SERVER_ONLY)

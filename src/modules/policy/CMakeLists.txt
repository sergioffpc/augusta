# Game policy (ADR-0022): the rules' hooks, the views built for them and the
# typed Policy actions read from their answers, which SimulationWorld and the
# scenario loader call. Over the sandboxed Lua embedding (augusta_scripting),
# which no header of its own names, so its callers never link it. The Tick in a
# view's on_tick is the simulation's time (augusta/tick.h).
add_library(augusta_policy STATIC answers.cpp policy.cpp)
target_include_directories(augusta_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_policy PUBLIC augusta_tick PRIVATE augusta_scripting)
augusta_set_module_scope(augusta_policy SERVER_ONLY)

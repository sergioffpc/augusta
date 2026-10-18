# The simulation's time - the Tick, the command Sequence and the fixed schedule
# both tick loops keep - in a module of its own that depends on nothing (see
# tick.h), so the protocol and both loops take it without the gameplay rules.
add_library(augusta_tick STATIC tick.cpp)
target_include_directories(augusta_tick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_tick SHARED)

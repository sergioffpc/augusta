# The fixed tick schedule both the server's Simulation loop and the client's
# Prediction loop keep; pure, so tested with a fake clock (see tick.h).
add_library(augusta_tick STATIC tick.cpp)
target_include_directories(augusta_tick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_tick PUBLIC augusta_primitives)
augusta_set_module_scope(augusta_tick SHARED)

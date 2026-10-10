# The conversions both peers make between the protocol and the shared core's
# types (see shared_wire.h): server/wire.h and the harness's harness_wire.h
# call them rather than each keeping a copy.
add_library(augusta_shared_wire STATIC shared_wire.cpp)
target_include_directories(augusta_shared_wire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_shared_wire
  PUBLIC augusta_assets augusta_command augusta_physics augusta_protocol augusta_tick
)
augusta_set_module_scope(augusta_shared_wire SHARED)

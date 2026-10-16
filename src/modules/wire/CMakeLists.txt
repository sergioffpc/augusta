# The conversions both peers make between the protocol and the shared core's
# types (see augusta/wire.h): augusta/host_wire.h and the harness's harness_wire.h
# call them rather than each keeping a copy.
add_library(augusta_wire STATIC wire.cpp)
target_include_directories(augusta_wire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_wire PUBLIC augusta_assets augusta_gameplay augusta_physics augusta_protocol)
augusta_set_module_scope(augusta_wire SHARED)

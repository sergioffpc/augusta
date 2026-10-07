# The counters and bounds the engine and the Networking Protocol share, in a
# module of their own that depends on nothing (see primitives.h): header-only,
# so linking it never pulls physics, the tick schedule or the protocol in.
add_library(augusta_primitives INTERFACE)
target_include_directories(augusta_primitives INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_primitives SHARED)

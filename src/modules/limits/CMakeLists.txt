# The bounds the gameplay rules and the Networking Protocol share, in a module of
# their own that depends on nothing (see limits.h): header-only, so linking it
# never pulls the rules or the protocol in.
add_library(augusta_limits INTERFACE)
target_include_directories(augusta_limits INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_limits SHARED)

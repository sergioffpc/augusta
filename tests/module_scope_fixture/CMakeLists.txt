# A build graph for server_boundary.cmake to configure: whether the
# client/server scope check (cmake/AugustaModule.cmake) rejects a server-only
# target that reaches a client-only one, directly or through a shared one.
# EDGE picks the link: NONE (no crossing), DIRECT or THROUGH_SHARED.
cmake_minimum_required(VERSION 3.25)
project(augusta_module_scope_fixture NONE)

include(${AUGUSTA_SOURCE_DIR}/cmake/AugustaModule.cmake)

add_library(fixture_input INTERFACE)
augusta_set_module_scope(fixture_input CLIENT_ONLY)

add_library(fixture_config INTERFACE)
augusta_set_module_scope(fixture_config SHARED)

add_library(fixture_server INTERFACE)
augusta_set_module_scope(fixture_server SERVER_ONLY)
target_link_libraries(fixture_server INTERFACE fixture_config)

if(EDGE STREQUAL "DIRECT")
  target_link_libraries(fixture_server INTERFACE fixture_input)
elseif(EDGE STREQUAL "THROUGH_SHARED")
  target_link_libraries(fixture_config INTERFACE fixture_input)
endif()

augusta_check_module_scopes()

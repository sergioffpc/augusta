add_library(augusta_input STATIC input.cpp)
target_include_directories(augusta_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_input PUBLIC augusta_command augusta_math PRIVATE augusta_physics)
# Client-only (ARCHITECTURE.md §5's "Input handling"): the headless server never
# samples a device, and the Command it receives is augusta_command's (shared).
# The client's config file names its keys and controls through
# augusta_client_config, so the shared augusta_config does not need it either;
# tests/server_boundary.cmake keeps it that way.
augusta_set_module_scope(augusta_input CLIENT_ONLY)

# Shared, unlike augusta_input's device sampler (ARCHITECTURE.md §5): the client
# samples and sends a Command, and the server validates, queues and simulates
# the same struct, so neither SimulationWorld nor the server's command queue
# links client-only input code.
add_library(augusta_command INTERFACE)
target_include_directories(augusta_command INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_command INTERFACE augusta_physics)
augusta_set_module_scope(augusta_command SHARED)

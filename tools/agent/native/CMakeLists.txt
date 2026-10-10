# augusta_agent's native part (ADR-0052): an Agent, a player a Python script
# controls through Intents, and the pybind11 module the augusta_agent package
# wraps it with. Built by the root build with AUGUSTA_TOOLS and the tools
# environment's Python (the root CMakeLists.txt).

# An Agent on its Session and Runner (see agent.h): no Python in it.
add_library(augusta_agent_core STATIC agent.cpp)
target_include_directories(augusta_agent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_agent_core
  PUBLIC augusta_assets augusta_command augusta_harness augusta_physics
  PRIVATE augusta_failure augusta_map augusta_prediction
)
augusta_set_module_scope(augusta_agent_core CLIENT_ONLY)

# The in-process server the package's tests play against (see
# testing_server.h).
add_library(augusta_agent_testing_server STATIC testing_server.cpp)
target_include_directories(augusta_agent_testing_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_agent_testing_server
  PUBLIC augustad_content augustad_host
  PRIVATE augusta_assets augusta_failure augusta_parameters
)
augusta_set_module_scope(augusta_agent_testing_server SERVER_ONLY)

# Built straight into the package's source directory, so `import
# augusta_agent._native` works once the package is installed editable (uv
# sync in tools/), as the cooker's modules are (tools/pack/cpp). Untagged, as
# a test executable is: it holds an Agent and the test-only server in one
# process.
set(AUGUSTA_AGENT_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/augusta_agent")
pybind11_add_module(augusta_agent_py module.cpp)
target_link_libraries(
  augusta_agent_py
  PRIVATE
    augusta_agent_core
    augusta_agent_testing_server
    augusta_command
    augusta_failure
    augusta_harness
    augusta_logging
    augusta_math
    augusta_networking
    augusta_physics
)
set_target_properties(
  augusta_agent_py
  PROPERTIES
    OUTPUT_NAME "_native"
    RUNTIME_OUTPUT_DIRECTORY "${AUGUSTA_AGENT_PACKAGE_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${AUGUSTA_AGENT_PACKAGE_DIR}"
)

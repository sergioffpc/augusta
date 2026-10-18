# The Agents' native part (ADR-0052): the agent module, the Agent's Runtime,
# and the pybind11 module the augusta_agent package wraps it with. Built by the root build with AUGUSTA_TOOLS; the module only
# with the tools environment's Python (the root CMakeLists.txt).

# The Agent's Runtime (augusta/agent.h, ADR-0060): one harness::Session per
# Agent, run by its harness::Runner and driven by Intents, with no window, GPU
# or Python. Speaks to the server through the Harness alone, so it links no
# protocol or networking of its own; physics, whose map collision and bodies
# agent.h names, it links itself.
add_library(augusta_agent STATIC agent.cpp agent_report.cpp)
target_include_directories(augusta_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_agent
  PUBLIC augusta_assets augusta_gameplay augusta_harness augusta_physics
  PRIVATE augusta_failure augusta_prediction
)
augusta_set_module_scope(augusta_agent CLIENT_ONLY)

# The in-process server the package's tests and the C++ tests of whole Matches
# play against (see testing_server.h).
add_library(augusta_agent_testing_server STATIC testing_server.cpp)
target_include_directories(augusta_agent_testing_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_agent_testing_server
  PUBLIC augusta_scenario augusta_host
  PRIVATE augusta_assets augusta_failure augusta_gameplay
)
augusta_set_module_scope(augusta_agent_testing_server SERVER_ONLY)

if(NOT AUGUSTA_AGENT_PYTHON)
  return()
endif()

# Built straight into the package's source directory, so `import
# augusta_agent._native` works once the package is installed editable (uv
# sync in tools/), as the cooker's modules are (tools/pack/native). Untagged, as
# a test executable is: it holds an Agent and the test-only server in one
# process.
set(AUGUSTA_AGENT_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/augusta_agent")
pybind11_add_module(augusta_agent_py module.cpp)
target_link_libraries(
  augusta_agent_py
  PRIVATE
    augusta_agent
    augusta_agent_testing_server
    augusta_failure
    augusta_gameplay
    augusta_harness
    augusta_link
    augusta_logging
    augusta_math
    augusta_physics
)
set_target_properties(
  augusta_agent_py
  PROPERTIES
    OUTPUT_NAME "_native"
    RUNTIME_OUTPUT_DIRECTORY "${AUGUSTA_AGENT_PACKAGE_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${AUGUSTA_AGENT_PACKAGE_DIR}"
)

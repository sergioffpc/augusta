# What the server loads from its verified pack at startup (ARCHITECTURE.md §5):
# the scenario it serves, the Parameters its simulation and every client run on
# and its Game policy, which policy's loader reads (augusta/policy_loader.h). One
# module, two targets, so the Parameters loader keeps the seam its own tests
# drive it through.

# Reads the Parameters script through Lua (ADR-0022, ADR-0039) into the shared
# Parameters; pure, so tested with scripts as strings (see
# augusta/parameters_loader.h). Server-only: a client never reads the script, it
# is sent the result, so it never links the interpreter.
add_library(augusta_scenario_parameters_loader STATIC parameters_loader.cpp)
target_include_directories(augusta_scenario_parameters_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_scenario_parameters_loader PUBLIC augusta_gameplay PRIVATE augusta_lua_sandbox)
augusta_set_module_scope(augusta_scenario_parameters_loader SERVER_ONLY)

# The scenario, the Parameters and the Game policy as one loaded whole, which
# the Host is handed and never loads itself (see augusta/content.h).
add_library(augusta_scenario STATIC content.cpp)
target_include_directories(augusta_scenario PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_scenario
  PUBLIC augusta_assets augusta_failure augusta_gameplay augusta_math augusta_physics augusta_policy
  PRIVATE augusta_logging augusta_policy_loader augusta_scenario_parameters_loader
)
augusta_set_module_scope(augusta_scenario SERVER_ONLY)

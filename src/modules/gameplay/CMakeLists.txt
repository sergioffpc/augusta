# The shared core's gameplay rules both Worlds apply identically
# (ARCHITECTURE.md §5), below the Networking Protocol and Input. Its headers
# name physics, math, tick and limits types, hence PUBLIC.
add_library(augusta_gameplay STATIC ballistics.cpp parameters.cpp weapon.cpp)
target_include_directories(augusta_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_gameplay PUBLIC augusta_limits augusta_math augusta_physics augusta_tick)
augusta_set_module_scope(augusta_gameplay SHARED)

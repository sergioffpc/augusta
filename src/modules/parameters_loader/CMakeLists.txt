# Lua (ADR-0022) reads the server's Parameters script (ADR-0039) through sol2.
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(augusta_parameters_loader STATIC parameters_loader.cpp)
target_include_directories(augusta_parameters_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(augusta_parameters_loader SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(augusta_parameters_loader
  PUBLIC augusta_parameters
  PRIVATE sol2::sol2 ${LUA_LIBRARIES}
)
augusta_set_module_scope(augusta_parameters_loader SERVER_ONLY)

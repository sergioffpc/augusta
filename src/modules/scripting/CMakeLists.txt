find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

# The Lua sandbox (ADR-0022, ADR-0039) every script the server runs is loaded
# into: the Parameters script (server/parameters_loader) and the Game policy
# scripts (augusta_scripting). A target of its own, so sol2 reaches only the
# code that embeds Lua, not every module that includes scripting.h.
add_library(augusta_lua_sandbox STATIC sandbox/lua_sandbox.cpp)
target_include_directories(augusta_lua_sandbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sandbox/include)
target_include_directories(augusta_lua_sandbox SYSTEM PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(augusta_lua_sandbox PUBLIC sol2::sol2 ${LUA_LIBRARIES})
augusta_set_module_scope(augusta_lua_sandbox SERVER_ONLY)

add_library(augusta_scripting STATIC scripting.cpp)
target_include_directories(augusta_scripting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_scripting PRIVATE augusta_lua_sandbox)
augusta_set_module_scope(augusta_scripting SERVER_ONLY)

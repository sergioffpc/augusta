add_executable(augusta_server main.cpp)
target_link_libraries(augusta_server PRIVATE augusta_shared spdlog::spdlog)

add_executable(augusta_server main.cpp)
target_link_libraries(augusta_server PRIVATE augusta_version augusta_logging spdlog::spdlog)

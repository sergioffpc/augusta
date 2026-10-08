# The application boundary augustac's and augustad's main() run behind
# (ADR-0033): one classified outcome, one terminal log line, one exit status;
# no thread or dependency of its own, so tested directly (see application.h).
add_library(augusta_application STATIC application.cpp)
target_include_directories(augusta_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_application PUBLIC augusta_failure PRIVATE augusta_logging)
augusta_set_module_scope(augusta_application SHARED)

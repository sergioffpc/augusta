# The lock-free counter, gauge and histogram the server's threads write in
# place, and their Prometheus form (see augusta/lock_free_metrics.h, ADR-0049).
find_package(prometheus-cpp CONFIG REQUIRED)
add_library(augusta_metrics STATIC lock_free_metrics.cpp)
target_include_directories(augusta_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_metrics PUBLIC prometheus-cpp::core)
augusta_set_module_scope(augusta_metrics SERVER_ONLY)

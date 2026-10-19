# The runtime supervisor both Runtimes and the Harness run their fixed threads
# under (ADR-0005): plain threads and no clock, so tested directly (see
# supervisor.h). A worker's outcome is an augusta_failure Failure, hence PUBLIC.
add_library(augusta_supervisor STATIC supervisor.cpp)
target_include_directories(augusta_supervisor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_supervisor PUBLIC augusta_failure PRIVATE augusta_logging)
augusta_set_module_scope(augusta_supervisor SHARED)

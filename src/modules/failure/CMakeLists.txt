# The shared error model and controlled fault injection the Client, the
# Server and the shared modules use (ADR-0033); no dependencies, so tested
# directly (see failure.h and faults.h).
add_library(augusta_failure STATIC failure.cpp)
target_include_directories(augusta_failure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_failure SHARED)

# How an executable starts, runs its worker threads, stops and ends: the
# runtime Supervisor both runtimes run their fixed threads under (ADR-0005) and
# the application boundary augustac's and augustad's main() run behind
# (ADR-0033). Plain threads and no clock, so tested directly (see supervisor.h
# and application.h). Their outcome is an augusta_failure Failure, hence PUBLIC.
add_library(augusta_lifecycle STATIC supervisor.cpp application.cpp)
target_include_directories(augusta_lifecycle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_lifecycle PUBLIC augusta_failure PRIVATE augusta_logging)
augusta_set_module_scope(augusta_lifecycle SHARED)

# The runtime supervisor both runtimes run their fixed threads under
# (ADR-0005); plain threads and no clock, so tested directly (see supervisor.h).
add_library(augusta_supervisor STATIC supervisor.cpp)
target_include_directories(augusta_supervisor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_supervisor PRIVATE augusta_logging)
augusta_set_module_scope(augusta_supervisor SHARED)

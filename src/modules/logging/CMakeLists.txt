# Boost.Log (ADR-0036) writes the console sink; it stays behind logging.cpp, so
# the modules that log only see logging.h.
find_package(boost_log CONFIG REQUIRED)

add_library(augusta_logging STATIC logging.cpp)
target_include_directories(augusta_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_logging PRIVATE Boost::log)

# AUGUSTA_LOG_ACTIVE_LEVEL must reach every translation unit that expands the
# logging macros (they compile away under it - see logging.h), so it is a
# public definition: 0 is trace, 2 is info.
target_compile_definitions(augusta_logging PUBLIC
  $<$<CONFIG:Debug>:AUGUSTA_LOG_ACTIVE_LEVEL=0>
  $<$<NOT:$<CONFIG:Debug>>:AUGUSTA_LOG_ACTIVE_LEVEL=2>
)
# logging.cpp includes <windows.h> for the console mode; keep its min/max
# macros out of the way of the standard library.
target_compile_definitions(augusta_logging PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_logging SHARED)

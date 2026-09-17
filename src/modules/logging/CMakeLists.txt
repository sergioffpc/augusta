add_library(augusta_logging INTERFACE)
target_include_directories(augusta_logging INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_logging INTERFACE spdlog::spdlog)

# SPDLOG_ACTIVE_LEVEL must be defined before any translation unit first
# includes a spdlog header, so it's passed as a compile definition here
# rather than from within logging.h (whose own include order can't be
# guaranteed relative to other headers that pull in spdlog).
target_compile_definitions(augusta_logging INTERFACE
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)
augusta_set_module_scope(augusta_logging SHARED)

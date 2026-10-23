# A Development build's pcapng of the messages it sends and receives
# (ADR-0076): the pure encoder of its blocks, and the file writer that
# implements the transport's tap over it. A debug tool, so a Shipping build has
# no augusta_pcapng for anything to link (ADR-0074). It depends on nothing
# above networking's tap types, the failure model's faults and logging.
add_library(augusta_pcapng STATIC file_writer.cpp pcapng.cpp)
target_include_directories(augusta_pcapng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_pcapng
  # The tap it implements, and the faults its writes take.
  PUBLIC augusta_networking augusta_failure
  # The WARN line a stopped file is logged with.
  PRIVATE augusta_logging
)
augusta_set_module_scope(augusta_pcapng SHARED)

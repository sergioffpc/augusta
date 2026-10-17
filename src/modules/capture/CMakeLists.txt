# What a Match capture's records say, in the engine's types (see
# capture_records.h): apart, so the Link converts them without the file's
# reading and writing, which is built over it.
add_library(augusta_capture_records INTERFACE)
target_include_directories(augusta_capture_records INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_capture_records INTERFACE augusta_assets augusta_gameplay augusta_math)
augusta_set_module_scope(augusta_capture_records SHARED)

# A Match capture's file (ADR-0050): its engine-typed capture, read from and
# written to a stream by the server's Capturer, Replay and a Captured player
# (see capture.h). The format's framing, magic, header version and record
# order, and the Networking Protocol's records it is made of, stay inside: the
# protocol and the Link's conversions are linked privately, so no reader
# reaches them through this module (ADR-0038).
add_library(augusta_capture STATIC capture.cpp)
target_include_directories(augusta_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_capture
  PUBLIC augusta_capture_records augusta_failure
  PRIVATE augusta_link_messages augusta_protocol
)
augusta_set_module_scope(augusta_capture SHARED)

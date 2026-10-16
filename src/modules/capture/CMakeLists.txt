# A Match capture's file (ADR-0050): its engine-typed capture, read from and
# written to a stream by the server's Capturer, Replay and a Captured player
# (see capture.h). The format's framing, magic, header version and record
# order, and the Networking Protocol's records it is made of, stay inside: the
# protocol and the shared wire conversions are linked privately, so no reader
# reaches them through this module (ADR-0038).
add_library(augusta_capture STATIC capture.cpp)
target_include_directories(augusta_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_capture
  PUBLIC augusta_assets augusta_failure augusta_gameplay augusta_math
  PRIVATE augusta_protocol augusta_wire
)
augusta_set_module_scope(augusta_capture SHARED)

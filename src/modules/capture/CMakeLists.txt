# A Match capture's file (ADR-0050): its engine-typed capture, read from and
# written to a stream, and beneath it the format's framing, magic, header
# version and record order, read and written alike by the server's Capturer,
# Replay and a Captured player (see capture.h).
add_library(augusta_capture STATIC capture.cpp)
target_include_directories(augusta_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_capture
  PUBLIC augusta_assets augusta_command augusta_failure augusta_math augusta_protocol
  PRIVATE augusta_shared_wire
)
augusta_set_module_scope(augusta_capture SHARED)

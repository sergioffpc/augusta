# A Match capture's file format (ADR-0050): its framing, magic, header version
# and record order, read and framed alike by the server's Capturer and a
# Captured player (see capture_file.h).
add_library(augusta_capture_file STATIC capture_file.cpp)
target_include_directories(augusta_capture_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_capture_file PUBLIC augusta_protocol)
augusta_set_module_scope(augusta_capture_file SHARED)

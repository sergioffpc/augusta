# The Link's conversions between the game's state and the Networking Protocol's
# messages, both ways, and a Match capture's records (see
# augusta/link_messages.h, ADR-0038): stateless, with no transport beneath, so
# the harness, the Host and capture convert without linking networking.
add_library(augusta_link_messages STATIC capture_records.cpp link_messages.cpp)
target_include_directories(
  augusta_link_messages
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
  augusta_link_messages
  PUBLIC augusta_assets augusta_capture_records augusta_failure augusta_gameplay augusta_protocol
)
augusta_set_module_scope(augusta_link_messages SHARED)

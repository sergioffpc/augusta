# What each message says in the engine's terms (augusta/link_types.h): apart,
# so a module that speaks to the Link (augusta_link) takes them without the
# protocol beneath.
add_library(augusta_link_types INTERFACE)
target_include_directories(augusta_link_types INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_link_types
  INTERFACE augusta_assets augusta_gameplay augusta_math augusta_physics augusta_tick Boost::uuid
)
augusta_set_module_scope(augusta_link_types SHARED)

# The Link's conversions between the game's state and the Networking Protocol's
# messages, both ways, and a Match capture's records (see
# augusta/link_messages.h, ADR-0038): stateless, with no transport beneath, so
# the Host and capture convert without linking networking.
add_library(augusta_link_messages STATIC capture_records.cpp link_messages.cpp)
target_include_directories(augusta_link_messages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_link_messages
  PUBLIC augusta_capture_records augusta_failure augusta_link_types augusta_protocol
)
augusta_set_module_scope(augusta_link_messages SHARED)

# The Link itself (augusta/link.h, ADR-0038): its two ends, which send and
# receive the engine's types, each message with its reliability, and read their
# connections' numbers; the transport's process-wide setup and, for tests, its
# simulated conditions. With augusta_link_raw, the only target that links
# networking (checks/core_boundary.cmake). The conversions, the protocol and the
# transport are linked privately, so nothing that speaks to the Link reaches
# them through it.
add_library(augusta_link STATIC link.cpp)
target_include_directories(augusta_link PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_link
  PUBLIC augusta_failure augusta_link_types
  PRIVATE augusta_link_messages augusta_logging augusta_networking augusta_protocol
)
augusta_set_module_scope(augusta_link SHARED)

# For tests only: the Link's raw ends, which carry bytes as they are
# (raw_link.h), so a test speaks the protocol by hand. Apart from augusta_link,
# so no production target carries them; linked by tests alone.
add_library(augusta_link_raw STATIC raw_link.cpp)
target_include_directories(augusta_link_raw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augusta_link_raw PUBLIC augusta_failure augusta_link PRIVATE augusta_networking)
augusta_set_module_scope(augusta_link_raw SHARED)

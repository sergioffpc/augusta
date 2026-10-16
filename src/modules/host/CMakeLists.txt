# The Host: the server's socket and SimulationWorld without threads or a clock,
# the counterpart of the client's Harness (ADR-0060). The bookkeeping it keeps
# around a Match is pure, so each piece is its own target here and a test drives
# it without a network.

# Who is in the match and whether a join is admitted (see augusta/match.h).
add_library(augusta_host_match STATIC match.cpp)
target_include_directories(augusta_host_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_match PUBLIC augusta_assets augusta_math augusta_networking augusta_physics)
augusta_set_module_scope(augusta_host_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move (see
# augusta/command_queue.h).
add_library(augusta_host_commands STATIC command_queue.cpp)
target_include_directories(augusta_host_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_commands PUBLIC augusta_gameplay)
augusta_set_module_scope(augusta_host_commands SERVER_ONLY)

# The count of each peer's misbehaviour that decides when it is disconnected
# (see augusta/misbehaviour.h).
add_library(augusta_host_misbehaviour STATIC misbehaviour.cpp)
target_include_directories(augusta_host_misbehaviour PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_host_misbehaviour SERVER_ONLY)

# When a peer that has not been admitted is past its deadline (see
# augusta/admission.h).
add_library(augusta_host_admission STATIC admission.cpp)
target_include_directories(augusta_host_admission PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_host_admission PUBLIC augusta_networking)
augusta_set_module_scope(augusta_host_admission SERVER_ONLY)

# The counts behind the server's once-a-second heartbeat line (see
# augusta/heartbeat.h).
add_library(augusta_host_heartbeat STATIC heartbeat.cpp)
target_include_directories(augusta_host_heartbeat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_host_heartbeat SERVER_ONLY)

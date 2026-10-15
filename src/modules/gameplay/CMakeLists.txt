# The gameplay rules both Worlds apply identically, ARCHITECTURE.md §5's shared
# core: the Command, the fixed tick schedule (ADR-0005), the Parameters
# (ADR-0039), WeaponHandling (ADR-0023, ADR-0024) and Ballistics (ADR-0002,
# ADR-0044), one header and namespace each (see command.h, tick.h, parameters.h,
# weapon.h and ballistics.h). Shared, unlike augusta_input's device sampler: the
# server screens, queues and simulates the same Command the client sends, so it
# links no client input code. It stays below the Networking Protocol and the
# input sampler: neither is linked here, and the protocol links none of it. Its
# headers name physics, math and primitives types, hence PUBLIC.
add_library(augusta_gameplay STATIC ballistics.cpp parameters.cpp tick.cpp weapon.cpp)
target_include_directories(augusta_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_gameplay PUBLIC augusta_math augusta_physics augusta_primitives)
augusta_set_module_scope(augusta_gameplay SHARED)

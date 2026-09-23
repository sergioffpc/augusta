add_library(augusta_physics STATIC
  physics.cpp
)
target_include_directories(augusta_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_physics
  PUBLIC augusta_math
  PRIVATE augusta_logging augusta_protocol
)

if (MSVC)
  target_link_libraries(augusta_physics PRIVATE unofficial::omniverse-physx-sdk::sdk)
else()
  # PhysXCharacterKinematic and PhysXExtensions reference each other's
  # symbols (e.g. Cct::Controller needs PxRigidBodyExt, PxMeshOverlapUtil
  # needs Cct-side callbacks); GNU ld's single left-to-right archive pass
  # can't resolve that circular dependency in a fixed topological order
  # the way MSVC's linker tolerates, so the whole PhysX SDK needs
  # --start-group/--end-group here (MSVC has no equivalent flag, hence
  # the branch above).
  target_link_libraries(augusta_physics PRIVATE
    -Wl,--start-group unofficial::omniverse-physx-sdk::sdk -Wl,--end-group
  )
endif()
augusta_set_module_scope(augusta_physics SHARED)

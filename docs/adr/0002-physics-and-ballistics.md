# Physics & Ballistics

PhysX handles general collision and movement. Ballistics (bullet trajectories) is a custom-built module instead of relying on PhysX's generic projectile handling, since hand-rolling it is one of this project's core learning goals and gives full control over determinism.

## Raycasting a moved body

A World moves bodies only through PhysX character controllers and never simulates its scene. A controller's scene-query actor, the one a raycast reports a player hit on, is a kinematic actor, and the controller moves it only with `setKinematicTarget` (in `move`, and in `setPosition`/`setFootPosition`, which `resize` also goes through). A kinematic target takes effect at the next simulate, and scene queries see it earlier only if the actor has `PxRigidBodyFlag::eUSE_KINEMATIC_TARGET_FOR_SCENE_QUERIES`, which the controller does not set. So `physics::World::Raycast` hits a body where its controller was made, not where it is. `CreateBody` leaves the controller descriptor's position at its default, so that place is a capsule centered on the world origin, whatever the body's `initial_position`; a stance change resizes that capsule where it stands.

This was read from the PhysX 5.5.0 source the vcpkg port builds (tag `106.4-physx-5.5.0`: `CctController.cpp`, `CctCharacterController.cpp`, `CctCapsuleController.cpp`, `PxRigidDynamic.h`, `PxRigidBody.h`), not seen run: the confidence is high, but it is not verified. `StaticGeometryTest.ARaycastHitsAMovedBodyWhereItWasCreatedNotWhereItIs` asserts it, and says what to change if CI disagrees.

Player hit detection (M4, US-11) must first make each player's actor queryable where the body is, for instance by raising that flag on `PxController::getActor()` or by setting the actor's pose after each move. The fix belongs to M4.

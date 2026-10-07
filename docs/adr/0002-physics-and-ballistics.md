# Physics & Ballistics

PhysX handles general collision, movement and rigid-body dynamics (Props,
grenades and Cosmetic bodies, ADR-0045). Ballistics (bullet trajectories) is a
custom-built module instead of relying on PhysX's generic projectile handling,
since hand-rolling it is one of this project's core learning goals and gives
full control over determinism.

## Raycasting a moved body

A World moves players' bodies only through PhysX character controllers. A
controller's scene-query actor, the one a raycast reports a player hit on, is a
kinematic actor, and the controller moves it only with `setKinematicTarget` (in
`move`, and in `setPosition`/`setFootPosition`, which `resize` also goes
through). A kinematic target takes effect at the next simulate, and scene
queries see it earlier only if the actor has
`PxRigidBodyFlag::eUSE_KINEMATIC_TARGET_FOR_SCENE_QUERIES`, which the controller
does not set. So `physics::World::Raycast` hits a body where its controller
stood at the World's last simulate (ADR-0045's Dynamics phase), not where it is
after the moves since; before a World's first simulate, that place is where the
controller was made. `CreateBody` leaves the controller descriptor's position at
its default, so that first place is a capsule centered on the world origin,
whatever the body's `initial_position`; a stance change resizes that capsule
where it stands.

This was read from the PhysX 5.5.0 source the vcpkg port builds (tag
`106.4-physx-5.5.0`: `CctController.cpp`, `CctCharacterController.cpp`,
`CctCapsuleController.cpp`, `PxRigidDynamic.h`, `PxRigidBody.h`), not seen run:
the confidence is high, but it is not verified.
`StaticGeometryTest.ARaycastHitsAMovedBodyWhereItWasCreatedNotWhereItIs` asserts
it for a World that has not simulated, and says what to change if CI disagrees.

Player hit detection (M4, US-11) does not test against these actors: it tests a
shot against each player's hitboxes as they were the Shooter's delay ago, kept
in a history of recent ticks (ADR-0044), and against the Map and Props through
physics (ADR-0045). A shot's raycast must therefore never report the
controllers' actors, whether or not the World has simulated since they moved.
Any other query that must see players exactly where they are needs each actor
made queryable where the body is first, for instance by raising that flag on
`PxController::getActor()` or by setting the actor's pose after each move.

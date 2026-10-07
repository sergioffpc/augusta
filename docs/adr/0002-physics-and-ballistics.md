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

## A bullet's flight is bounded

A bullet ends on the Map, on a player, at its ammo's max range, or once it has
flown 5 seconds (`ballistics::kMaxFlightTime`), whichever comes first. The
Parameters' muzzle velocity and range are only required to be finite and above
0, so a valid pair can have a round crawl toward a range it never reaches; the
flight time is what keeps such a round from being simulated forever. A player
fires at most one round a tick, so each player the Match started with has fewer
than a flight time's worth of ticks of rounds in flight
(`ballistics::MaxFlightSteps`), and the Ballistics phase's work per tick has a
bound whatever the Parameters say. It is a resource bound, not a tuning value: a
rifle round reaches any range a Map has room for in a fraction of it, so it is a
constant shared by the server and the clients' tracers rather than a Parameter.
Server operators see the count as `augustad_bullets_in_flight` (ADR-0049).

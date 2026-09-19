#ifndef AUGUSTA_REPLICATION_H_
#define AUGUSTA_REPLICATION_H_

// augusta::replication will replicate SimulationWorld's per-tick
// Authoritative State (augusta::simulation::State, ADR-0023) to
// connected clients over augusta::networking. Scaffolded alongside
// augusta::simulation/prediction/presentation so the module boundary
// exists in the build, but deliberately left unimplemented for now -
// what it needs to send depends on those three ECS pipelines' entity/
// component shapes (not yet designed, see e.g. simulation.h) and on the
// Networking Protocol (ADR-0007), neither of which exist yet.
namespace augusta::replication {}  // namespace augusta::replication

#endif  // AUGUSTA_REPLICATION_H_

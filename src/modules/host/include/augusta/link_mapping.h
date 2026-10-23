#ifndef AUGUSTA_LINK_MAPPING_H_
#define AUGUSTA_LINK_MAPPING_H_

#include <cstdint>
#include <span>
#include <vector>

#include <boost/uuid/uuid.hpp>

#include "augusta/link_types.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/replay.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

/// \file
/// The conversions Host makes at the Link's edge (ADR-0038), the way
/// augusta/simulation_mapping.h makes them at SimulationWorld's: what Match,
/// replication and a Replay decide to send, which name players and bodies by
/// the server's own IDs, as the Link's messages (augusta/link_types.h), which
/// name them by the same numbers. A Session ID is one boost::uuids::uuid on
/// both sides, so it needs no conversion. The Link alone turns those into the
/// protocol's. What a client sends needs none: a join, a Replay request and a
/// command are the Link's own types on the server too.
namespace augusta::host {

[[nodiscard]] link::EntityId ToLink(EntityId entity);

/// A refusal no case names, a corrupted one, stays one: the Link then refuses
/// to encode it.
[[nodiscard]] link::JoinRefusal ToLink(JoinRefusal reason);

/// admission, with the tick rate and the parameters every client is told when it joins.
[[nodiscard]] link::Admission ToLink(const Admission& admission, std::uint8_t tick_rate_hz,
                                     const parameters::Parameters& parameters);

[[nodiscard]] link::Lobby ToLink(const Roster& roster);

/// start's players spawned where SimulationWorld put them: each of
/// start.players at the spawns entry of the same index; first_tick is the
/// Match's first tick, and match its Match ID.
[[nodiscard]] link::MatchStart ToLink(const MatchStart& start, std::span<const math::Vec3> spawns,
                                      tick::Tick first_tick, const boost::uuids::uuid& match);

[[nodiscard]] link::MatchEnd ToLink(const MatchEnd& end);

/// What replication planned for every recipient of a tick, before
/// FillRecipientFields fills in a recipient's own fields: the tick and every body.
[[nodiscard]] link::AuthoritativeState ToLink(const replication::Updates& updates);

/// Fills in state, as ToLink(Updates) made it, with what replication planned
/// for recipient alone, replacing any other recipient's: so one state is
/// addressed to each recipient in turn, its bodies mapped once.
void FillRecipientFields(link::AuthoritativeState& state, const replication::RecipientUpdate& recipient);

[[nodiscard]] link::Shot ToLink(const replication::Shot& shot);

[[nodiscard]] link::HitConfirmation ToLink(const replication::HitConfirmation& hit);

[[nodiscard]] link::Death ToLink(const replication::Death& death);

/// What each player of a Replay looked like on tick, as its viewer's Replay view.
[[nodiscard]] link::ReplayView ToLink(const std::vector<PlayerView>& views, tick::Tick tick);

#if AUGUSTA_DEBUG_TOOLS
/// How SimulationWorld judged a round, as its shooter is told it (ADR-0038):
/// an entry later in the flight than the Link's 16 bits carry is held to the
/// most they do.
[[nodiscard]] link::ShotJudgement ToLink(const simulation::ShotJudgement& judgement);
#endif

}  // namespace augusta::host

#endif  // AUGUSTA_LINK_MAPPING_H_

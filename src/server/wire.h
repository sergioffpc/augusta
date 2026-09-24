#ifndef AUGUSTA_SERVER_WIRE_H_
#define AUGUSTA_SERVER_WIRE_H_

#include <cstdint>
#include <vector>

#include "augusta/assets.h"
#include "augusta/command.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "command_queue.h"
#include "match.h"

// The server's edge with the Networking Protocol (ADR-0038): what server::Host
// sends, turned from the engine's types into the protocol's plain ones right
// before Encode, and what it receives, turned back right after Decode. The only
// place on the server where a protocol::*Wire type meets an engine type: Match,
// CommandQueue and replication never see one. Pure field-by-field copies;
// whether a value is one the server accepts is decided after, by whoever takes
// it in.
namespace augusta::server {

/// session as the protocol carries it.
[[nodiscard]] protocol::SessionIdWire ToWire(SessionId session);

/// reason as the protocol carries it.
[[nodiscard]] protocol::JoinRefusalWire ToWire(JoinRefusal reason);

/// admission as the message that tells the peer, with the tick rate and the
/// parameters every client is told when it joins.
[[nodiscard]] protocol::JoinAcceptedWire ToWire(const Admission& admission, std::uint8_t tick_rate_hz,
                                                const parameters::Parameters& parameters);

/// body as the protocol carries it.
[[nodiscard]] protocol::BodyStateWire ToWire(const physics::BodyState& body);

/// parameters as the protocol carries them.
[[nodiscard]] protocol::ParametersWire ToWire(const parameters::Parameters& parameters);

/// The Lobby's Roster as the protocol carries it.
[[nodiscard]] protocol::LobbyWire ToWire(const Roster& roster);

/// A match's start as the protocol carries it.
[[nodiscard]] protocol::MatchStartWire ToWire(const MatchStart& start);

/// What replication planned for one recipient, as the message it is sent.
[[nodiscard]] protocol::AuthoritativeStateWire ToWire(const replication::Update& update);

/// hash in the engine's terms.
[[nodiscard]] assets::PackHash FromWire(const protocol::PackHashWire& hash);

/// A join a client asked for, in the engine's terms.
[[nodiscard]] JoinRequest FromWire(const protocol::JoinRequestWire& request);

/// A command a client sent, in the engine's terms.
[[nodiscard]] command::Command FromWire(const protocol::CommandWire& command);

/// A sequenced command a client sent, in the engine's terms.
[[nodiscard]] SequencedCommand FromWire(const protocol::SequencedCommandWire& command);

/// The commands a client sent in one message, oldest first, in the engine's terms.
[[nodiscard]] std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_WIRE_H_

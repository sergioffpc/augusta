#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/protocol.h"

// Property-based tests of the codec (ADR-0013): every message within the
// protocol's limits survives Encode and Decode unchanged. RC_PARAMS sets the case
// count at run time (e.g. RC_PARAMS="max_success=10000"); pull requests run the
// default 100.
namespace augusta::protocol {

// How RapidCheck prints a failing case, found by argument-dependent lookup.
// Every field is printed, so a shrunk case shows which one did not survive.
void showValue(const math::Vec3& value, std::ostream& out) {
  out << "(" << value.x << ", " << value.y << ", " << value.z << ")";
}

void showValue(const CommandWire& command, std::ostream& out) {
  out << "{direction ";
  showValue(command.direction, out);
  out << ", yaw " << command.yaw << ", pitch " << command.pitch << ", flags " << +command.flags << ", stance "
      << +static_cast<std::uint8_t>(command.desired_stance) << "}";
}

void showValue(const BodyStateWire& body, std::ostream& out) {
  out << "{position ";
  showValue(body.position, out);
  out << ", velocity ";
  showValue(body.velocity, out);
  out << ", stamina " << body.stamina << ", flags " << +body.flags << ", stance "
      << +static_cast<std::uint8_t>(body.stance) << "}";
}

void showValue(const MessageWire& message, std::ostream& out) {
  struct Printer {
    std::ostream& out;

    void operator()(const JoinRequestWire& request) const {
      out << "JoinRequest{engine_version " << rc::toString(request.engine_version) << ", client_pack";
      for (const std::byte byte : request.client_pack) {
        out << " " << std::to_integer<int>(byte);
      }
      out << ", character " << rc::toString(request.character) << "}";
    }
    void operator()(const JoinAcceptedWire& accepted) const {
      out << "JoinAccepted{session " << static_cast<std::uint32_t>(accepted.session) << ", tick_rate_hz "
          << +accepted.tick_rate_hz << ", deplete " << accepted.parameters.stamina.deplete_per_second << ", regen "
          << accepted.parameters.stamina.regen_per_second << ", forced_walk_below "
          << accepted.parameters.stamina.forced_walk_below << ", player_count " << +accepted.parameters.player_count
          << ", character " << +accepted.character << "}";
    }
    void operator()(const JoinRefusedWire& refused) const {
      out << "JoinRefused{reason " << +static_cast<std::uint8_t>(refused.reason) << "}";
    }
    void operator()(const CommandsWire& commands) const {
      out << "Commands{";
      for (const SequencedCommandWire& command : commands.commands) {
        out << command.sequence << ": ";
        showValue(command.command, out);
        out << "; ";
      }
      out << "}";
    }
    void operator()(const AuthoritativeStateWire& state) const {
      out << "AuthoritativeState{tick " << state.tick << ", acknowledged " << state.acknowledged_sequence << ", ";
      for (const EntityStateWire& body : state.bodies) {
        out << static_cast<std::uint32_t>(body.entity) << ": ";
        showValue(body.body, out);
        out << "; ";
      }
      out << "}";
    }
    void operator()(const LobbyWire& lobby) const {
      out << "Lobby{version " << lobby.version << ", ";
      for (const RosterEntryWire& entry : lobby.roster) {
        out << static_cast<std::uint32_t>(entry.session) << ": " << +entry.character << "; ";
      }
      out << "}";
    }
    void operator()(const ReadyWire& ready) const { out << "Ready{version " << ready.version << "}"; }
    void operator()(const MatchStartWire& start) const {
      out << "MatchStart{";
      for (const MatchPlayerWire& player : start.players) {
        out << static_cast<std::uint32_t>(player.session) << ": entity " << static_cast<std::uint32_t>(player.entity)
            << ", character " << +player.character << ", spawn ";
        showValue(player.spawn, out);
        out << "; ";
      }
      out << "}";
    }
    void operator()(const MatchEndWire& /*end*/) const { out << "MatchEnd{}"; }
  };
  std::visit(Printer{.out = out}, message);
}

}  // namespace augusta::protocol

namespace {

using augusta::math::FromSteps;
using augusta::math::Grid;
using augusta::math::kAngleGrid;
using augusta::math::kDirectionGrid;
using augusta::math::kPositionGrid;
using augusta::math::kStaminaGrid;
using augusta::math::kVelocityGrid;
using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::Decode;
using augusta::protocol::Encode;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::JoinAcceptedWire;
using augusta::protocol::JoinRefusalWire;
using augusta::protocol::JoinRefusedWire;
using augusta::protocol::JoinRequestWire;
using augusta::protocol::kMaxCharacterPathLength;
using augusta::protocol::kMaxCommandsPerMessage;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::kMaxPlayers;
using augusta::protocol::kPackHashSize;
using augusta::protocol::LobbyWire;
using augusta::protocol::MatchEndWire;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::MatchStartWire;
using augusta::protocol::MessageWire;
using augusta::protocol::PackHashWire;
using augusta::protocol::ParametersWire;
using augusta::protocol::ReadyWire;
using augusta::protocol::RosterEntryWire;
using augusta::protocol::SequencedCommandWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::StaminaWire;
using augusta::protocol::StanceWire;

// A value on grid, anywhere in its range: the only numbers a body or a command
// holds (augusta/grid.h), so the only ones the codec must carry exactly.
rc::Gen<float> OnGrid(const Grid& grid) {
  // Counts 0, -1, 1, -2, 2 ... on a signed grid (whose min is -(max + 1)), so a
  // failing case shrinks toward 0 steps rather than toward the grid's minimum.
  const std::int64_t span = std::int64_t{grid.max} - grid.min + 1;
  return rc::gen::map(rc::gen::inRange<std::int64_t>(0, span), [grid](std::int64_t index) {
    const std::int64_t steps = grid.min >= 0 ? grid.min + index : (index % 2 == 0 ? index / 2 : -(index + 1) / 2);
    return FromSteps(static_cast<std::int32_t>(steps), grid);
  });
}

rc::Gen<Vec3> Vec3OnGrid(const Grid& grid) {
  return rc::gen::apply([](float x, float y, float z) { return Vec3(x, y, z); }, OnGrid(grid), OnGrid(grid),
                        OnGrid(grid));
}

// A float sent as its bits: any finite one (a NaN never equals itself).
rc::Gen<float> FiniteFloat() {
  return rc::gen::suchThat(rc::gen::arbitrary<float>(), [](float value) { return std::isfinite(value); });
}

// A string or a list of at most max_size elements.
template <typename Container, typename T>
rc::Gen<Container> UpTo(std::size_t max_size, rc::Gen<T> element) {
  return rc::gen::mapcat(rc::gen::inRange<std::size_t>(0, max_size + 1),
                         [element](std::size_t size) { return rc::gen::container<Container>(size, element); });
}

rc::Gen<StanceWire> Stance() {
  return rc::gen::element(StanceWire::kStanding, StanceWire::kCrouching, StanceWire::kProne);
}

// A character index: 1 to 255, never 0 (ADR-0042).
rc::Gen<std::uint8_t> Character() {
  return rc::gen::map(rc::gen::inRange(1, 256), [](int index) { return static_cast<std::uint8_t>(index); });
}

template <typename Id>
rc::Gen<Id> AnyId() {
  return rc::gen::map(rc::gen::arbitrary<std::uint32_t>(), [](std::uint32_t id) { return static_cast<Id>(id); });
}

rc::Gen<PackHashWire> PackHash() {
  return rc::gen::map(rc::gen::container<std::vector<std::uint8_t>>(kPackHashSize, rc::gen::arbitrary<std::uint8_t>()),
                      [](const std::vector<std::uint8_t>& bytes) {
                        PackHashWire hash{};
                        for (std::size_t i = 0; i < hash.size(); ++i) {
                          hash[i] = static_cast<std::byte>(bytes[i]);
                        }
                        return hash;
                      });
}

rc::Gen<CommandWire> Command() {
  return rc::gen::build<CommandWire>(
      rc::gen::set(&CommandWire::direction, Vec3OnGrid(kDirectionGrid)),
      rc::gen::set(&CommandWire::yaw, OnGrid(kAngleGrid)), rc::gen::set(&CommandWire::pitch, OnGrid(kAngleGrid)),
      rc::gen::set(&CommandWire::flags, rc::gen::inRange<std::uint8_t>(0, CommandWire::kReload << 1U)),
      rc::gen::set(&CommandWire::desired_stance, Stance()));
}

rc::Gen<BodyStateWire> Body() {
  return rc::gen::build<BodyStateWire>(
      rc::gen::set(&BodyStateWire::position, Vec3OnGrid(kPositionGrid)),
      rc::gen::set(&BodyStateWire::velocity, Vec3OnGrid(kVelocityGrid)),
      rc::gen::set(&BodyStateWire::stamina, OnGrid(kStaminaGrid)),
      rc::gen::set(&BodyStateWire::flags, rc::gen::element<std::uint8_t>(0, BodyStateWire::kExhausted)),
      rc::gen::set(&BodyStateWire::stance, Stance()));
}

rc::Gen<JoinRequestWire> JoinRequest() {
  return rc::gen::build<JoinRequestWire>(
      rc::gen::set(&JoinRequestWire::engine_version,
                   UpTo<std::string>(kMaxEngineVersionLength, rc::gen::arbitrary<char>())),
      rc::gen::set(&JoinRequestWire::client_pack, PackHash()),
      rc::gen::set(&JoinRequestWire::character,
                   UpTo<std::string>(kMaxCharacterPathLength, rc::gen::arbitrary<char>())));
}

rc::Gen<JoinAcceptedWire> JoinAccepted() {
  const auto stamina = rc::gen::build<StaminaWire>(rc::gen::set(&StaminaWire::deplete_per_second, FiniteFloat()),
                                                   rc::gen::set(&StaminaWire::regen_per_second, FiniteFloat()),
                                                   rc::gen::set(&StaminaWire::forced_walk_below, FiniteFloat()));
  const auto parameters =
      rc::gen::build<ParametersWire>(rc::gen::set(&ParametersWire::stamina, stamina),
                                     rc::gen::set(&ParametersWire::player_count, rc::gen::arbitrary<std::uint8_t>()));
  return rc::gen::build<JoinAcceptedWire>(
      rc::gen::set(&JoinAcceptedWire::session, AnyId<SessionIdWire>()),
      rc::gen::set(&JoinAcceptedWire::tick_rate_hz, rc::gen::arbitrary<std::uint8_t>()),
      rc::gen::set(&JoinAcceptedWire::parameters, parameters), rc::gen::set(&JoinAcceptedWire::character, Character()));
}

rc::Gen<JoinRefusedWire> JoinRefused() {
  return rc::gen::build<JoinRefusedWire>(rc::gen::set(
      &JoinRefusedWire::reason, rc::gen::element(JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kLobbyFull,
                                                 JoinRefusalWire::kUnknownCharacter, JoinRefusalWire::kMatchInProgress,
                                                 JoinRefusalWire::kPackMismatch)));
}

rc::Gen<CommandsWire> Commands() {
  const auto sequenced = rc::gen::build<SequencedCommandWire>(
      rc::gen::set(&SequencedCommandWire::sequence, rc::gen::arbitrary<std::uint32_t>()),
      rc::gen::set(&SequencedCommandWire::command, Command()));
  return rc::gen::build<CommandsWire>(rc::gen::set(
      &CommandsWire::commands, UpTo<std::vector<SequencedCommandWire>>(kMaxCommandsPerMessage, sequenced)));
}

rc::Gen<AuthoritativeStateWire> AuthoritativeState() {
  const auto entity = rc::gen::build<EntityStateWire>(rc::gen::set(&EntityStateWire::entity, AnyId<EntityIdWire>()),
                                                      rc::gen::set(&EntityStateWire::body, Body()));
  return rc::gen::build<AuthoritativeStateWire>(
      rc::gen::set(&AuthoritativeStateWire::tick, rc::gen::arbitrary<std::uint32_t>()),
      rc::gen::set(&AuthoritativeStateWire::acknowledged_sequence, rc::gen::arbitrary<std::uint32_t>()),
      rc::gen::set(&AuthoritativeStateWire::bodies, UpTo<std::vector<EntityStateWire>>(kMaxPlayers, entity)));
}

rc::Gen<LobbyWire> Lobby() {
  const auto entry = rc::gen::build<RosterEntryWire>(rc::gen::set(&RosterEntryWire::session, AnyId<SessionIdWire>()),
                                                     rc::gen::set(&RosterEntryWire::character, Character()));
  return rc::gen::build<LobbyWire>(
      rc::gen::set(&LobbyWire::version, rc::gen::arbitrary<std::uint32_t>()),
      rc::gen::set(&LobbyWire::roster, UpTo<std::vector<RosterEntryWire>>(kMaxPlayers, entry)));
}

rc::Gen<ReadyWire> Ready() {
  return rc::gen::build<ReadyWire>(rc::gen::set(&ReadyWire::version, rc::gen::arbitrary<std::uint32_t>()));
}

rc::Gen<MatchStartWire> MatchStart() {
  const auto player = rc::gen::build<MatchPlayerWire>(rc::gen::set(&MatchPlayerWire::spawn, Vec3OnGrid(kPositionGrid)),
                                                      rc::gen::set(&MatchPlayerWire::session, AnyId<SessionIdWire>()),
                                                      rc::gen::set(&MatchPlayerWire::entity, AnyId<EntityIdWire>()),
                                                      rc::gen::set(&MatchPlayerWire::character, Character()));
  return rc::gen::build<MatchStartWire>(
      rc::gen::set(&MatchStartWire::players, UpTo<std::vector<MatchPlayerWire>>(kMaxPlayers, player)));
}

// Any message of the protocol, within its limits.
rc::Gen<MessageWire> Message() {
  return rc::gen::oneOf(rc::gen::cast<MessageWire>(JoinRequest()), rc::gen::cast<MessageWire>(JoinAccepted()),
                        rc::gen::cast<MessageWire>(JoinRefused()), rc::gen::cast<MessageWire>(Commands()),
                        rc::gen::cast<MessageWire>(AuthoritativeState()), rc::gen::cast<MessageWire>(Lobby()),
                        rc::gen::cast<MessageWire>(Ready()), rc::gen::cast<MessageWire>(MatchStart()),
                        rc::gen::just(MessageWire{MatchEndWire{}}));
}

RC_GTEST_PROP(ProtocolPropertyTest, EveryMessageSurvivesEncodeThenDecode, ()) {
  const MessageWire message = *Message();

  const auto decoded = Decode(Encode(message));

  RC_ASSERT(decoded.has_value());
  RC_ASSERT(*decoded == message);
}

}  // namespace

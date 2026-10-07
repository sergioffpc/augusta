#include "host_log.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/ballistics.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "match.h"

namespace augusta::server {

namespace {

std::string WinnerName(const std::optional<SessionId>& winner) {
  return winner.has_value() ? std::to_string(SessionNumber(*winner)) : "draw";
}

std::string_view EndReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::kWinCondition:
      return "win condition";
    case EndReason::kNoPlayersLeft:
      return "no players left";
    case EndReason::kEndedByTheHost:
      return "ended by the host";
  }
  std::unreachable();
}

std::string_view BodyPartName(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return "head";
    case ballistics::BodyPart::kTorso:
      return "torso";
    case ballistics::BodyPart::kLimb:
      return "limb";
  }
  std::unreachable();
}

}  // namespace

std::uint32_t PeerNumber(networking::PeerId peer) { return static_cast<std::uint32_t>(peer); }

std::uint32_t SessionNumber(SessionId session) { return static_cast<std::uint32_t>(session); }

std::string_view LeavingName(Leaving how) {
  switch (how) {
    case Leaving::kLeft:
      return "left";
    case Leaving::kTimedOut:
      return "timeout";
    case Leaving::kMisbehaving:
      return "misbehaving";
  }
  std::unreachable();
}

void LogMatchEnded(EndReason reason, const std::optional<SessionId>& winner, tick::Tick ticks, std::size_t players,
                   std::uint32_t roster_version) {
  LI("subsystem=serverruntime event=match_ended reason=\"{}\" winner={} ticks={} players={} version={}",
     EndReasonName(reason), WinnerName(winner), ticks, players, roster_version);
}

void LogCombat(const simulation::State& state) {
  for (const simulation::Hit& hit : state.hits) {
    LI("subsystem=serverruntime event=hit tick={} shooter={} target={} part={} damage={} health={}", state.tick,
       std::to_underlying(hit.shooter), std::to_underlying(hit.target), BodyPartName(hit.part), hit.damage, hit.health);
  }
  for (const simulation::Death& death : state.deaths) {
    LI("subsystem=serverruntime event=death tick={} victim={} killer={} part={}", state.tick,
       std::to_underlying(death.victim), std::to_underlying(death.killer), BodyPartName(death.part));
  }
}

}  // namespace augusta::server

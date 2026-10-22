#include "augusta/host_log.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "augusta/body_part.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/match.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"

namespace augusta::host {

namespace {

std::string WinnerName(const std::optional<SessionId>& winner) {
  return winner.has_value() ? boost::uuids::to_string(*winner) : "draw";
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

}  // namespace

std::uint32_t PeerNumber(link::PeerId peer) { return static_cast<std::uint32_t>(peer); }

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

void LogMatchEnded(const boost::uuids::uuid& match, EndReason reason, const std::optional<SessionId>& winner,
                   tick::Tick ticks, std::size_t players, std::uint32_t roster_version) {
  LOG_INFO("subsystem=serverruntime event=match_ended match={} reason=\"{}\" winner={} ticks={} players={} version={}",
           boost::uuids::to_string(match), EndReasonName(reason), WinnerName(winner), ticks, players, roster_version);
}

void LogCombat(const boost::uuids::uuid& match, const simulation::State& state) {
  for (const simulation::Hit& hit : state.hits) {
    LOG_INFO("subsystem=serverruntime event=hit match={} tick={} shooter={} target={} part={} damage={} health={}",
             boost::uuids::to_string(match), state.tick, std::to_underlying(hit.shooter),
             std::to_underlying(hit.target), body_part::NameOf(hit.part), hit.damage, hit.health);
  }
  for (const simulation::Death& death : state.deaths) {
    LOG_INFO("subsystem=serverruntime event=death match={} tick={} victim={} killer={} part={}",
             boost::uuids::to_string(match), state.tick, std::to_underlying(death.victim),
             std::to_underlying(death.killer), body_part::NameOf(death.part));
  }
}

}  // namespace augusta::host

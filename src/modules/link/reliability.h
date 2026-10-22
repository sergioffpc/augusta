#ifndef AUGUSTA_LINK_RELIABILITY_H_
#define AUGUSTA_LINK_RELIABILITY_H_

#include "augusta/link_types.h"
#include "augusta/networking.h"

/// \file
/// Which reliability the Link sends each message with, private to the module so
/// it is tested apart from a live connection: ADR-0038's table, which no caller
/// names.
namespace augusta::link {

/// How a message of type travels: unreliable when a newer one supersedes it (a
/// tick's Commands, an Authoritative State, a Replay view), reliable when its
/// loss would leave the two sides disagreeing (everything else).
constexpr networking::Reliability ReliabilityOf(MessageType type) {
  switch (type) {
    case MessageType::kCommands:
    case MessageType::kAuthoritativeState:
    case MessageType::kReplayView:
      return networking::Reliability::kUnreliable;
    case MessageType::kJoinRequest:
    case MessageType::kJoinAccepted:
    case MessageType::kJoinRefused:
    case MessageType::kLobby:
    case MessageType::kReady:
    case MessageType::kMatchStart:
    case MessageType::kMatchEnd:
    case MessageType::kShot:
    case MessageType::kHitConfirmation:
    case MessageType::kDeath:
    case MessageType::kReplayListRequest:
    case MessageType::kReplayList:
    case MessageType::kReplayRequest:
    case MessageType::kReenactRequest:
#if AUGUSTA_DEBUG_TOOLS
    case MessageType::kDebugSubscribe:
#endif
      return networking::Reliability::kReliable;
  }
  // Every type the protocol carries is named above.
  return networking::Reliability::kReliable;
}

}  // namespace augusta::link

#endif  // AUGUSTA_LINK_RELIABILITY_H_

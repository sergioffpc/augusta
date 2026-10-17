#include <gtest/gtest.h>

#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/networking.h"
#include "connection_stats.h"
#include "reliability.h"

// What the Link decides about the transport beneath it, apart from a live
// connection: which reliability each message travels with (ADR-0038's table),
// and the connection's numbers in the Link's own types.
namespace {

using augusta::link::ConnectionStats;
using augusta::link::FromTransport;
using augusta::link::MessageType;
using augusta::link::ReliabilityOf;
using augusta::networking::Reliability;

// A newer one supersedes each of these, so none is retransmitted: a tick's
// Commands, an Authoritative State and a Replay view (ADR-0051).
TEST(ReliabilityOfTest, PerTickMessagesTravelUnreliably) {
  for (const MessageType type : {MessageType::kCommands, MessageType::kAuthoritativeState, MessageType::kReplayView}) {
    EXPECT_EQ(ReliabilityOf(type), Reliability::kUnreliable) << static_cast<int>(type);
  }
}

// Each of these must arrive, or the two sides disagree.
TEST(ReliabilityOfTest, EveryOtherMessageTravelsReliably) {
  for (const MessageType type : {
           MessageType::kJoinRequest,
           MessageType::kJoinAccepted,
           MessageType::kJoinRefused,
           MessageType::kLobby,
           MessageType::kReady,
           MessageType::kMatchStart,
           MessageType::kMatchEnd,
           MessageType::kShot,
           MessageType::kHitConfirmation,
           MessageType::kDeath,
           MessageType::kReplayListRequest,
           MessageType::kReplayList,
           MessageType::kReplayRequest,
           MessageType::kReenactRequest,
       }) {
    EXPECT_EQ(ReliabilityOf(type), Reliability::kReliable) << static_cast<int>(type);
  }
}

// Every number reaches the Link's own type as the transport measured it, the
// "not measured yet" sentinels included.
TEST(FromTransportTest, KeepsEveryNumberTheTransportMeasured) {
  const ConnectionStats stats = FromTransport(augusta::networking::ConnectionStats{
      .ping_ms = 42,
      .quality_local = 0.75F,
      .quality_remote = -1.0F,
      .in_bytes_per_sec = 1200.0F,
      .out_bytes_per_sec = 3400.0F,
      .max_jitter_us = 5600,
      .pending_bytes = 78,
  });
  EXPECT_EQ(stats.ping_ms, 42);
  EXPECT_FLOAT_EQ(stats.quality_local, 0.75F);
  EXPECT_FLOAT_EQ(stats.quality_remote, -1.0F);
  EXPECT_FLOAT_EQ(stats.in_bytes_per_sec, 1200.0F);
  EXPECT_FLOAT_EQ(stats.out_bytes_per_sec, 3400.0F);
  EXPECT_EQ(stats.max_jitter_us, 5600);
  EXPECT_EQ(stats.pending_bytes, 78);
}

}  // namespace

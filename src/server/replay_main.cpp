// augusta_replay: replays a match recording augustad wrote (ADR-0050) on a
// fresh SimulationWorld and checks every tick resolves what it recorded.
//
//   augusta_replay <recording> <server pack> <public key> [--across-builds]
//
// The pack must be the one the recording names. Without --across-builds the
// outcome must match bit for bit, which holds on the build that recorded it;
// with it, positions may be a grid step off (replay.h). Exits 0 when every tick
// matches, 1 when one diverges, and 2 when the replay cannot start.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <print>
#include <span>
#include <string_view>
#include <utility>

#include "augusta/assets.h"
#include "augusta/logging.h"
#include "augusta/version.h"
#include "content.h"
#include "recording.h"
#include "replay.h"

namespace {

constexpr int kSameOutcome = 0;
constexpr int kDiverged = 1;
constexpr int kCannotReplay = 2;

constexpr std::string_view kUsage = "usage: augusta_replay <recording> <server pack> <public key> [--across-builds]";

// One line per body of outcome, for comparing a divergent tick by eye.
void PrintBodies(std::string_view label, const augusta::server::TickOutcome& outcome) {
  for (const auto& body : outcome.bodies) {
    std::println("  {} body {}: position ({}, {}, {}) velocity ({}, {}, {}) yaw {} health {} rounds {}", label,
                 static_cast<std::uint32_t>(body.entity), body.body.position.x, body.body.position.y,
                 body.body.position.z, body.body.velocity.x, body.body.velocity.y, body.body.velocity.z, body.yaw,
                 body.health, body.rifle.rounds);
  }
  std::println("  {}: {} shots, {} hits, {} deaths, match end {}", label, outcome.shots.size(), outcome.hits.size(),
               outcome.deaths.size(), outcome.match_end.has_value());
}

int Run(std::span<char*> arguments) {
  const bool across_builds = arguments.size() == 5 && std::string_view(arguments[4]) == "--across-builds";
  if (arguments.size() != 4 && !across_builds) {
    std::println(stderr, "{}", kUsage);
    return kCannotReplay;
  }
  const std::filesystem::path recording_path{arguments[1]};
  const std::filesystem::path pack_path{arguments[2]};
  const std::filesystem::path key_path{arguments[3]};

  std::ifstream file(recording_path, std::ios::binary);
  const auto recording = augusta::server::ReadRecording(file);
  if (!recording.has_value()) {
    std::println(stderr, "{}: {}", recording_path.string(), augusta::server::DescribeRecordingError(recording.error()));
    return kCannotReplay;
  }
  if (recording->torn) {
    std::println("{}: ends partway through a tick, which is left out", recording_path.string());
  }

  const auto pack = augusta::assets::LoadVerifiedPack(pack_path, key_path);
  if (!pack.has_value()) {
    std::println(stderr, "{}", augusta::assets::DescribeVerifiedPackError(pack.error(), pack_path, key_path));
    return kCannotReplay;
  }
  if (pack->Hash() != recording->header.server_pack) {
    std::println(stderr, "{} is not the server pack the recording was made on", pack_path.string());
    return kCannotReplay;
  }
  auto content = augusta::server::LoadServerContent(*pack, recording->header.tick_rate_hz);
  if (!content.has_value()) {
    std::println(stderr, "{}: {}", pack_path.string(), augusta::server::DescribeContentError(content.error()));
    return kCannotReplay;
  }
  if (recording->header.engine_version != augusta::EngineVersion()) {
    std::println("recorded by engine {}, replayed by {}: outcomes may differ", recording->header.engine_version,
                 augusta::EngineVersion());
  }

  const auto replayed = augusta::server::Replay(
      *recording, *std::move(content), across_builds ? augusta::server::kAcrossBuilds : augusta::server::kSameBuild);
  if (!replayed.has_value()) {
    const augusta::server::Divergence& divergence = replayed.error();
    std::println("diverged on tick {} of {}: {}", divergence.tick, recording->ticks.size(),
                 augusta::server::DescribeDivergenceKind(divergence.kind));
    PrintBodies("recorded", divergence.recorded);
    PrintBodies("replayed", divergence.replayed);
    return kDiverged;
  }
  std::println("{} ticks replayed to the recorded outcome", *replayed);
  return kSameOutcome;
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();
  return Run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
}

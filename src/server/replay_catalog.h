#ifndef AUGUSTA_SERVER_REPLAY_CATALOG_H_
#define AUGUSTA_SERVER_REPLAY_CATALOG_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "capture.h"

/// \file
/// Which Match captures a replay server replays (ADR-0051): those of its
/// directory (`replay.captures`) made on its own server pack, at its tick
/// rate, by players of its scenario's Characters. ReplayCatalog lists them and
/// finds one by name, matching the name against the directory's listing and
/// never opening it as a path, so a name from the network reaches no file
/// outside the directory. It reads the directory afresh each time, so a
/// capture a live server writes into it is listed once it is whole enough to
/// read. server::ReplayServer asks it on the Network I/O thread.
namespace augusta::server {

/// What a capture must have been made on for a replay server to replay it:
/// a Replay needs its server pack's Map, Characters and Game policy, and runs
/// at the server's tick rate.
struct ReplayTerms {
  assets::PackHash server_pack{};
  std::uint8_t tick_rate_hz = 0;
  /// The scenario's Characters, by path (ADR-0042).
  std::vector<std::string> characters;
};

/// One capture a replay server replays, as a Replay list names it.
struct ReplayListing {
  /// Its file name, which a Replay request names it by.
  std::string name;
  /// When its Match started, in UTC.
  std::chrono::sys_time<std::chrono::milliseconds> started;
  /// How long it lasted, in ticks at tick_rate_hz: up to its Match end, or
  /// to its last record if it ends partway.
  std::uint32_t ticks = 0;
  std::uint8_t tick_rate_hz = 0;
  /// Its players' Characters, in its Join order.
  std::vector<std::string> characters;

  bool operator==(const ReplayListing&) const = default;
};

/// What a Replay viewer asks for, as a Replay request carries it.
struct ReplayRequest {
  /// The client's engine version (augusta::EngineVersion).
  std::string engine_version;
  /// The hash of the client pack the client loaded.
  assets::PackHash client_pack{};
  /// The capture to watch, by its name in the Replay list: matched, never opened.
  std::string capture;
};

/// capture, read from the file name, as a Replay list names it, or nullopt if
/// a replay server on terms does not replay it: made on another pack or at
/// another rate, with no player, or with a Character the scenario lacks.
[[nodiscard]] std::optional<ReplayListing> ListingOf(std::string name, const Capture& capture,
                                                     const ReplayTerms& terms);

/// The captures of one directory a replay server replays.
class ReplayCatalog {
 public:
  ReplayCatalog(std::filesystem::path directory, ReplayTerms terms);

  /// Every capture of the directory it replays, ordered by name, which for
  /// one server's captures is when they started (CaptureFileName). A file
  /// that does not read as a capture is left out.
  [[nodiscard]] std::vector<ReplayListing> List() const;

  /// The capture of the directory's listing named name, read, if it is one
  /// List lists; nullopt for any other name, a path among them.
  [[nodiscard]] std::optional<Capture> Find(std::string_view name) const;

 private:
  // The regular files of the directory whose names end in ".capture", by name.
  [[nodiscard]] std::vector<std::filesystem::directory_entry> Entries() const;

  std::filesystem::path directory_;
  ReplayTerms terms_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_REPLAY_CATALOG_H_

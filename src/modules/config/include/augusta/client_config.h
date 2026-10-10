#ifndef AUGUSTA_CLIENT_CONFIG_H_
#define AUGUSTA_CLIENT_CONFIG_H_

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "augusta/config.h"
#include "augusta/input.h"

/// \file
/// The client's startup settings, augustac.yaml, read under augusta::config's
/// rules (ADR-0034), and the options its command line takes besides the shared
/// ones: `--reenact <capture> --player <n>`, which make one run a Captured
/// player (ADR-0050). Client-only: it binds the player's controls, so it is the
/// one config that depends on client Input (see augusta/server_config.h for
/// the server's).
namespace augusta::config {

/// The client's default config file, looked up next to augustac.
inline constexpr std::string_view kClientConfigFileName = "augustac.yaml";

/// Default server the client connects to (ARCHITECTURE.md §3: direct IP:port).
inline constexpr std::string_view kDefaultServerAddress = "127.0.0.1:27015";

/// What augustac.yaml holds. Its required key `base_dir` is where the relative
/// paths below start from; it is applied, not kept.
struct ClientConfig {
  /// Key `content.pack` (required): the client pack to load.
  std::filesystem::path pack_path;
  /// Key `content.public_key` (required): the Ed25519 public key the pack is signed with.
  std::filesystem::path public_key_path;
  /// Key `player.character` (required): the character to play, by its name
  /// in the scenario's manifest (e.g. "soldier"). The server admits
  /// only one of its scenario's (ADR-0042).
  std::string character;
  /// Key `network.server_address`: the server to connect to.
  std::string server_address{kDefaultServerAddress};
  /// Key `logging.level`: one of "trace", "debug", "info", "warn", "error",
  /// "critical" - the console sink's runtime floor (augusta::logging::SetLogLevel).
  /// Only lowers what the build already compiles in (AUGUSTA_LOG_ACTIVE_LEVEL);
  /// a Release build has no TRACE/DEBUG to raise it back to.
  std::string log_level{kDefaultLogLevel};
  /// Key `input.mouse_sensitivity` (a finite number above zero) and section
  /// `input.keys` (control name -> key name, e.g. `sprint: Space`), both
  /// optional: how the player's controls respond and which key triggers each.
  /// Controls the section leaves out keep their input::kDefaultKeymap key; no
  /// two controls may share a key, and none may use input::kReleaseCursorKey.
  /// An `input.keys` entry it rejects is a kInvalidEntry error, its subject the
  /// entry (`input.keys.jump`) and its reason what is wrong with it.
  input::Config input{};
};

/// Parses a client config from yaml_text. A relative `base_dir` key is resolved
/// against base_dir (the file's directory), and the other relative paths
/// against the key. The error's subject is the key that is wrong.
std::expected<ClientConfig, ConfigError> ParseClientConfig(std::string_view yaml_text,
                                                           const std::filesystem::path& base_dir);

/// Reads and parses the client config at file; its `base_dir` is relative to
/// file's directory. Errors carry file.
std::expected<ClientConfig, ConfigError> LoadClientConfig(const std::filesystem::path& file);

/// What augustac's command line takes beyond `--config`, `--help` and
/// `--version`: a Replay (ADR-0051) or a capture to reenact (ADR-0050) is
/// chosen for one run, so it is asked for there, not in augustac.yaml
/// (ADR-0034).
inline constexpr std::array<CommandLineOption, 4> kClientOptions{{
    {.name = "replays", .value = {}, .description = "print the captures the server replays, then exit"},
    {.name = "replay", .value = "capture", .description = "watch the capture of this name the server replays"},
    {.name = "reenact", .value = "capture", .description = "reenact a player of this Match capture file"},
    {.name = "player", .value = "n", .description = "the player of the capture to reenact, from 1"},
}};

/// What one run of augustac does.
enum class ClientMode : std::uint8_t {
  /// Joins the server and plays, or reenacts a capture's player (ReadReenactArguments).
  kPlay,
  /// Asks the replay server for its Replay list, prints it and exits (`--replays`).
  kListReplays,
  /// Watches one capture on the replay server (`--replay <capture>`).
  kWatchReplay,
};

/// A run of augustac, as its command line asks for it.
struct ClientRun {
  ClientMode mode = ClientMode::kPlay;
  /// The capture to watch, by its name in the Replay list; only for kWatchReplay.
  std::string capture;
};

/// The run command_line, read with kClientOptions, asks for; kInvalidArguments
/// if it asks to list the captures and watch one at once, or names no capture.
std::expected<ClientRun, ConfigError> ReadClientRun(const CommandLine& command_line);

/// What `--reenact <capture> --player <n>` asks for: to play player n of the
/// capture again against the configured server.
struct ReenactArguments {
  /// The capture file, taken as given (relative to the working directory).
  std::filesystem::path capture;
  /// The player's number in the capture, from 1.
  std::uint8_t player = 0;
};

/// What command_line, read with kClientOptions, asks to reenact: nullopt with
/// neither `--reenact` nor `--player`, which plays as a person does. A
/// kInvalidArguments error with one but not the other, with `--replays` or
/// `--replay` too, an empty capture, or a player that is not a whole number
/// from 1 to 255.
std::expected<std::optional<ReenactArguments>, ConfigError> ReadReenactArguments(const CommandLine& command_line);

}  // namespace augusta::config

#endif  // AUGUSTA_CLIENT_CONFIG_H_

#include <csignal>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/config.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/version.h"
#include "host.h"
#include "parameters_loader.h"
#include "policy_loader.h"
#include "runtime.h"

namespace {

// Signal handlers can't capture context, so this is the only way to
// reach the one ServerRuntime main() constructs - set just before
// Run() is called, never reassigned afterward.
augusta::runtime::ServerRuntime* g_runtime = nullptr;

extern "C" void HandleShutdownSignal(int /*signal*/) {
  if (g_runtime != nullptr) {
    g_runtime->Stop();
  }
}

// The pack file_config names, verified against its public key. Called before
// anything else starts (no socket, world, or thread is spun up yet) - a bad
// pack or key means this process exits there, never partially running against
// untrusted content (ADR-0018, ARCHITECTURE.md §8). Reports what is wrong and
// returns nullopt.
std::optional<augusta::assets::Pack> LoadVerifiedPack(const augusta::config::ServerConfig& file_config) {
  const std::filesystem::path& pack_path = file_config.pack_path;
  const std::filesystem::path& public_key_path = file_config.public_key_path;

  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(public_key_path);
  if (!public_key) {
    LE("subsystem=server event=public_key_loading_failed path={} error=public_key_unreadable",
       public_key_path.string());
    return std::nullopt;
  }
  auto pack = augusta::assets::Pack::Load(pack_path, *public_key);
  if (!pack) {
    LE("subsystem=server event=pack_verification_failed path={} error={}", pack_path.string(),
       augusta::assets::DescribeLoadError(pack.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=pack_verified path={}", pack_path.string());
  return *std::move(pack);
}

// Each of paths as a Character with its hitboxes and its eye, in the same
// order, or nullopt after reporting the first that has no hitbox for a body
// part or no eye: every hit on a player resolves to a body part (US-11) and
// every Shot leaves from its shooter's eye (US-07), so the server runs only on
// characters it can judge and fire for.
std::optional<std::vector<augusta::server::Character>> LoadCharacters(const augusta::assets::Pack& pack,
                                                                      std::vector<std::string> paths,
                                                                      const std::filesystem::path& pack_path) {
  std::vector<augusta::server::Character> characters;
  characters.reserve(paths.size());
  for (std::string& path : paths) {
    auto hitboxes = pack.ResolveHitboxes(path);
    if (!hitboxes) {
      LE("subsystem=server event=hitboxes_loading_failed path={} character={} error={}", pack_path.string(), path,
         augusta::assets::DescribeResolveError(hitboxes.error(), "hitbox"));
      return std::nullopt;
    }
    if (const auto missing = augusta::assets::FirstMissingBodyPart(*hitboxes)) {
      LE("subsystem=server event=hitboxes_loading_failed path={} character={} error=\"no hitbox for the {}\"",
         pack_path.string(), path, augusta::assets::BodyPartName(*missing));
      return std::nullopt;
    }
    const auto eye = pack.ResolveEye(augusta::assets::CharacterEyePath(path));
    if (!eye) {
      LE("subsystem=server event=eye_loading_failed path={} character={} error={}", pack_path.string(), path,
         augusta::assets::DescribeResolveError(eye.error(), "eye"));
      return std::nullopt;
    }
    characters.push_back({.path = std::move(path), .hitboxes = *std::move(hitboxes), .eye = eye->position});
  }
  return characters;
}

// Built before any socket or thread starts, so a pack without a usable map
// exits like a bad pack does. Reports what is wrong and returns nullopt.
std::optional<augusta::server::Map> LoadMap(const augusta::assets::Pack& pack, const std::filesystem::path& pack_path) {
  auto collision = augusta::map::LoadCollision(pack);
  if (!collision) {
    LE("subsystem=server event=collision_loading_failed path={} error={}", pack_path.string(),
       augusta::map::DescribeMapError(collision.error()));
    return std::nullopt;
  }
  auto spawn_points = augusta::map::LoadSpawnPoints(pack);
  if (!spawn_points) {
    LE("subsystem=server event=spawn_points_loading_failed path={} error={}", pack_path.string(),
       augusta::map::DescribeMapError(spawn_points.error()));
    return std::nullopt;
  }
  // The scenario's characters, the only ones a player may join as (ADR-0042).
  auto character_paths = pack.ResolveCharacters();
  if (!character_paths) {
    LE("subsystem=server event=characters_loading_failed path={} asset={} error={}", pack_path.string(),
       augusta::assets::kCharactersPath,
       augusta::assets::DescribeResolveError(character_paths.error(), "character list"));
    return std::nullopt;
  }
  auto characters = LoadCharacters(pack, *std::move(character_paths), pack_path);
  if (!characters) {
    return std::nullopt;
  }
  // The client pack cooked with this one, the only one a player may join with.
  const auto client_pack = pack.ResolveClientPackHash();
  if (!client_pack) {
    LE("subsystem=server event=client_pack_hash_loading_failed path={} asset={} error={}", pack_path.string(),
       augusta::assets::kClientPackPath,
       augusta::assets::DescribeResolveError(client_pack.error(), "client pack hash"));
    return std::nullopt;
  }
  LI("subsystem=server event=map_loaded colliders={} spawn_points={} characters={}", collision->size(),
     spawn_points->size(), characters->size());
  return augusta::server::Map{
      .collision = *std::move(collision),
      .spawn_points = *std::move(spawn_points),
      .characters = *std::move(characters),
      .client_pack = *client_pack,
  };
}

// The scenario's Parameters script, out of the pack the server was given (it was
// cooked into the server pack with the map and is signed with it), evaluated
// once before any socket or thread starts, so a pack without one, or with one
// that does not load, exits like a bad pack does. The Parameters are the same
// for the whole run. Reports what is wrong and returns nullopt.
std::optional<augusta::parameters::Parameters> LoadParameters(const augusta::assets::Pack& pack,
                                                              const std::filesystem::path& pack_path) {
  const std::string_view script_path = augusta::assets::kParametersScriptPath;
  const auto script = pack.ResolveScript(script_path);
  if (!script) {
    LE("subsystem=server event=parameters_script_loading_failed path={} script={} error={}", pack_path.string(),
       script_path, augusta::assets::DescribeResolveError(script.error(), "script"));
    return std::nullopt;
  }
  auto parameters = augusta::parameters::Load(*script);
  if (!parameters) {
    LE("subsystem=server event=parameters_loading_failed path={} script={} error={}", pack_path.string(), script_path,
       augusta::parameters::DescribeLoadError(parameters.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=parameters_loaded script={}", script_path);
  return *std::move(parameters);
}

// The scenario's Game policy scripts (ADR-0022), out of the same pack and
// loaded before any socket or thread starts, so a script that does not load
// exits like a bad Parameters script does. A scenario may lack either script.
// Reports what is wrong and returns nullopt.
std::optional<augusta::scripting::Engine> LoadPolicy(const augusta::assets::Pack& pack,
                                                     const std::filesystem::path& pack_path) {
  auto policy = augusta::server::LoadPolicy(pack);
  if (!policy) {
    LE("subsystem=server event=policy_loading_failed path={} script={} error=\"{}\"", pack_path.string(),
       augusta::scripting::ScriptPath(policy.error().script), augusta::server::DescribePolicyLoadError(policy.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=policy_loaded");
  return *std::move(policy);
}

// At most one round fires a tick, so a scenario whose rifle asks for more fires
// slower than its script says: worth a warning, not a refusal.
void WarnIfTheRifleOutpacesTheTick(const augusta::parameters::Rifle& rifle, std::uint8_t tick_rate_hz) {
  if (augusta::parameters::FiresFasterThanTheTickRate(rifle, tick_rate_hz)) {
    LW("subsystem=server event=rifle_fire_rate_capped rounds_per_minute={} tick_rate_hz={}", rifle.rounds_per_minute,
       tick_rate_hz);
  }
}

// What ServerRuntime's Config is built from: the file's settings and the
// pack's Parameters script. The pack's map travels to ServerRuntime
// separately (see main()), not through Config.
augusta::runtime::Config BuildRuntimeConfig(const augusta::config::ServerConfig& file_config,
                                            const augusta::parameters::Parameters& parameters) {
  augusta::runtime::Config config;
  config.listen.address = file_config.listen_address;
  config.tick_rate_hz = file_config.tick_rate_hz;
  // Every client is sent the rate and these when it joins and predicts with
  // them, so the config file and the scenario's script are the only places they
  // are set.
  config.parameters = parameters;
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  augusta::logging::Init();

  // Settings come from a config file - augustad.yaml next to the executable
  // unless --config names another (ADR-0034) - not from the command line,
  // which otherwise only asks for --help or --version (printed, then exit).
  const auto command_line = augusta::config::ParseCommandLine(
      argc, argv, "augustad", augusta::config::kServerConfigFileName, augusta::EngineVersion());
  if (command_line && command_line->action != augusta::config::CommandLineAction::kRun) {
    std::println("{}", command_line->message);
    return 0;
  }
  const auto file_config = command_line.and_then(
      [](const augusta::config::CommandLine& read) { return augusta::config::LoadServerConfig(read.config_file); });
  if (!file_config) {
    LE("subsystem=server event=config_loading_failed error={}",
       augusta::config::DescribeConfigError(file_config.error()));
    return 1;
  }
  // ParseServerConfig already validated log_level, so this is never nullopt.
  augusta::logging::SetLogLevel(*augusta::logging::ParseSeverity(file_config->log_level));
  LI("subsystem=server event=starting version={}", augusta::EngineVersion());

  const auto pack = LoadVerifiedPack(*file_config);
  if (!pack) {
    return 1;
  }

  auto map = LoadMap(*pack, file_config->pack_path);
  if (!map) {
    return 1;
  }

  const auto parameters = LoadParameters(*pack, file_config->pack_path);
  if (!parameters) {
    return 1;
  }
  WarnIfTheRifleOutpacesTheTick(parameters->rifle, file_config->tick_rate_hz);

  auto policy = LoadPolicy(*pack, file_config->pack_path);
  if (!policy) {
    return 1;
  }

  // augusta::networking::Init() must run once, process-wide, before any
  // Client/Server is constructed - see networking.h.
  augusta::networking::Init();

  const augusta::runtime::Config config = BuildRuntimeConfig(*file_config, *parameters);
  augusta::runtime::ServerRuntime runtime(config, *std::move(map), *std::move(policy));
  g_runtime = &runtime;
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);

  // A failure was logged by the supervisor where it happened; it only sets the exit status here.
  return runtime.Run().has_value() ? 1 : 0;
}

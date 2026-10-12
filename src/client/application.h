#ifndef AUGUSTA_CLIENT_APPLICATION_H_
#define AUGUSTA_CLIENT_APPLICATION_H_

#include <expected>
#include <memory>
#include <optional>

#include "augusta/assets.h"
#include "augusta/client_config.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/reenactment.h"
#include "character_loader.h"
#include "content.h"

/// \file
/// How augustac's phases classify their failures behind the application
/// boundary (augusta/application.h, ADR-0033): an unusable config or capture to
/// reenact, a transport
/// that does not start, a pack or content that does not load, a Session that
/// ends on its own, a character that cannot be loaded. main.cpp runs these as
/// its Lifecycle around ClientRuntime (runtime.h), which classifies its run's
/// failures with the functions below and passes its workers' first cause on as
/// the supervisor recorded it. Nothing here needs a window or a GPU, so it is
/// tested without the renderer.
namespace augusta::client {

/// The config file command_line names, read, or the
/// failure::Code::kInvalidConfiguration failure saying why command_line or the
/// file is unusable. A command line asking for help or the version is main's
/// to answer before this.
[[nodiscard]] std::expected<config::ClientConfig, failure::Failure> ReadClientConfig(
    const std::expected<config::CommandLine, config::ConfigError>& command_line);

/// The Script of the player command_line asks to reenact (`--reenact <capture>
/// --player <n>`, ADR-0050), or nullopt if it asks for none; or the
/// failure::Code::kInvalidConfiguration failure saying why the arguments, the
/// capture or its player are unusable, its context naming the capture's path.
[[nodiscard]] std::expected<std::optional<harness::Script>, failure::Failure> ReadReenactment(
    const config::CommandLine& command_line);

/// Whether script's capture may be reenacted with the client pack of hash
/// loaded: the failure::Code::kInvalidConfiguration failure if it names
/// another, whose Commands and spawns this pack's Map and Characters need not
/// fit.
[[nodiscard]] std::expected<void, failure::Failure> CheckReenactmentPack(const harness::Script& script,
                                                                         const assets::PackHash& loaded);

/// The verified pack and what a ClientRuntime is made from. The runtime holds
/// on to the pack (it loads the characters other players bring from it), so
/// the pack keeps its address.
struct LoadedClient {
  std::unique_ptr<const assets::Pack> pack;
  Content content;
};

/// Verifies the pack file_config names and loads the content of the character
/// it asks to play, or returns the failure::Code::kInvalidContent failure, its
/// context naming the pack's path. Verified before anything else starts (no
/// renderer/audio device, network socket, or thread is spun up yet) - a bad
/// pack or key means this process exits here, never partially running against
/// untrusted content (ADR-0018, ARCHITECTURE.md §8).
[[nodiscard]] std::expected<LoadedClient, failure::Failure> LoadClient(const config::ClientConfig& file_config);

/// A Session that ended on its own, classified: a refused join is
/// failure::Code::kJoinRefused, a server that never admitted this client
/// kServerUnreachable, and a connection lost after admission
/// kPeerConnectionLost. The detail is the sentence the player is told.
[[nodiscard]] failure::Failure ClassifySessionFailure(const harness::Failure& ended);

/// A character that could not be loaded in the Lobby, as the
/// failure::Code::kInvalidContent failure naming it under `character=`.
[[nodiscard]] failure::Failure ClassifyCharacterError(const CharacterError& error);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_APPLICATION_H_

#ifndef AUGUSTA_CLIENT_APPLICATION_H_
#define AUGUSTA_CLIENT_APPLICATION_H_

#include <memory>
#include <optional>
#include <string>

#include <boost/uuid/uuid.hpp>

#include "augusta/application.h"
#include "augusta/client_config.h"
#include "augusta/reenactment.h"

/// \file
/// augustac's runs behind the application boundary (application.h, ADR-0033),
/// once its config file and command line are read (client_phases.h): listing a
/// replay server's Replays, or the Lifecycle that initializes the transport,
/// loads the client pack's content, constructs the ClientRuntime (client.h)
/// from it and runs it. Each classifies its own failure, so main() only
/// concludes the one Outcome. Each runs at the raised Windows timer resolution
/// (application::TimerResolution): the Lifecycle through application::Execute,
/// the listing by holding one itself.
namespace augusta::client {

/// One augustac run's runtime: the verified pack, the ClientRuntime that reads
/// from it (it loads the characters other players bring from it, so the pack
/// outlives it).
class ClientProcess {
 public:
  /// What it holds, made by ClientLifecycle's construct phase.
  struct Parts;

  explicit ClientProcess(std::unique_ptr<Parts> parts);
  ~ClientProcess();

  ClientProcess(const ClientProcess&) = delete;
  ClientProcess& operator=(const ClientProcess&) = delete;
  ClientProcess(ClientProcess&&) = delete;
  ClientProcess& operator=(ClientProcess&&) = delete;

  /// ClientRuntime::Run(), on the thread that constructed it.
  [[nodiscard]] application::Outcome Run();

 private:
  std::unique_ptr<Parts> parts_;
};

/// augustac --replays (ADR-0051): asks the replay server file_config names for
/// the captures it replays and prints them, or says why it could not: the
/// transport that does not start, the server that does not answer within 10
/// seconds or refuses, as the Session's failures are classified
/// (client_phases.h). A server address that does not parse is classified where
/// it is refused; anything else escaping is failure::Code::kWorkerFailed, with
/// `phase=list_replays`.
[[nodiscard]] application::Outcome ListReplays(const config::ClientConfig& file_config);

/// augustac's Lifecycle for file_config: initialize starts the transport
/// (link::Init); construct verifies the pack and loads the content of the
/// character to play, then constructs the ClientRuntime - as reenactment's
/// Captured player if it has one (ADR-0050), playing its capture's Character
/// rather than the config's, or as the Replay viewer of the capture replay
/// names (ADR-0051) - whose own exception the application boundary classifies:
/// a dependency's (no window or GPU device), or one already classified (a
/// rejected collision mesh); run runs it until the window closes, its
/// reenactment or Replay ends or it fails. With no reconnecting and no
/// connection screen, it says what happened and exits. Its join, as a player,
/// a Captured player or a Replay viewer alike, carries run, the Run's ID.
[[nodiscard]] application::Lifecycle<ClientProcess> ClientLifecycle(const config::ClientConfig& file_config,
                                                                    std::optional<harness::Script> reenactment,
                                                                    std::optional<std::string> replay,
                                                                    boost::uuids::uuid run);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_APPLICATION_H_

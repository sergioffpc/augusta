#ifndef AUGUSTA_AGENT_TESTING_SERVER_H_
#define AUGUSTA_AGENT_TESTING_SERVER_H_

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <thread>

#include "content.h"
#include "host.h"

/// \file
/// A server::Host in the script's own process, for the augusta_agent package's
/// tests only (augusta_agent._testing): what the C++ tests of whole Matches run
/// against, so a test needs no augustad.
namespace augusta::agent::testing {

/// A Host serving a scenario on a free loopback port, ticked at its tick rate
/// on a thread of its own until it is destroyed.
class Server {
 public:
  /// Verifies the server pack at pack_path with the key at public_key_path and
  /// serves its scenario for player_count players: one alone never ends a
  /// Match by last player standing. Throws std::runtime_error, with why, if
  /// the pack or its content does not load.
  Server(const std::filesystem::path& pack_path, const std::filesystem::path& public_key_path,
         std::uint8_t player_count);

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
  ~Server() = default;

  /// Where an Agent connects to it.
  [[nodiscard]] std::string Address() const;

 private:
  Server(server::Content content, std::uint8_t player_count);
  void Serve(const std::stop_token& stop);

  server::Host host_;
  // Last, so it is joined before the Host goes.
  std::jthread thread_;
};

}  // namespace augusta::agent::testing

#endif  // AUGUSTA_AGENT_TESTING_SERVER_H_

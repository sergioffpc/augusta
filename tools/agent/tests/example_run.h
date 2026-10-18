#ifndef AUGUSTA_AGENT_TESTS_EXAMPLE_RUN_H_
#define AUGUSTA_AGENT_TESTS_EXAMPLE_RUN_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "augusta/agent.h"
#include "augusta/assets.h"
#include "augusta/link.h"
#include "match_loop.h"

/// \file
/// What the C++ tests of whole Matches share (ADR-0013): the example
/// scenario's packs, a run of Agents against a server, and the transport's
/// process-wide setup.
namespace augusta::agent::testing {

inline constexpr const char* kCharacter = "soldier";

// The transport set up once for the whole test process, and torn down after:
// see link::Shutdown. Each test file registers one.
class NetworkingEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(link::Init().has_value()); }
  void TearDown() override { link::Shutdown(); }
};

inline std::filesystem::path ExamplePacks() { return AUGUSTA_EXAMPLE_PACKS; }

// The example's client pack, verified with the test key, as an Agent loads it.
inline LoadedPack ExampleClientPack() {
  auto pack = LoadPack((ExamplePacks() / "client").replace_extension(augusta::assets::kPackExtension),
                       ExamplePacks() / "test.pub");
  if (!pack.has_value()) {
    ADD_FAILURE() << DescribeLoadPackError(
        pack.error(), (ExamplePacks() / "client").replace_extension(augusta::assets::kPackExtension),
        ExamplePacks() / "test.pub");
    return {};
  }
  return *std::move(pack);
}

// A run against server through pack, every Agent to see matches Match ends
// within timeout.
inline MatchLoopConfig RunAgainst(const std::string& server, const LoadedPack& pack, std::uint32_t matches,
                                  std::chrono::steady_clock::duration timeout) {
  return MatchLoopConfig{
      .server = server,
      .pack = &pack,
      .character = kCharacter,
      .matches = matches,
      .timeout = timeout,
      .seed = 1,
  };
}

}  // namespace augusta::agent::testing

#endif  // AUGUSTA_AGENT_TESTS_EXAMPLE_RUN_H_

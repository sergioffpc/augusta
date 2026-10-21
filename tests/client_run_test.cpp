#include "augusta/client_run.h"

#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "augusta/logging.h"
#include "augusta/uuid.h"

// The client's Session in its log (ADR-0029): from its admission to its end,
// every line carries `session=`, and none does after.
namespace {

using augusta::client::SessionLogContext;
using augusta::logging::Severity;

const std::optional<augusta::uuid::Uuid> kSession = augusta::uuid::Parse("01900000-0000-7000-8000-000000000011");

// What one LOG_INFO of message writes to the console.
std::string WrittenInfoLine(std::string_view message) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(Severity::kInfo);
  testing::internal::CaptureStdout();
  LOG_INFO("{}", message);
  return testing::internal::GetCapturedStdout();
}

TEST(SessionLogContextTest, EveryLineWhileTheSessionIsHeldCarriesItsSessionId) {
  ASSERT_TRUE(kSession.has_value());
  const SessionLogContext held(*kSession);

  const std::string written = WrittenInfoLine("subsystem=test event=admitted");

  EXPECT_NE(written.find("event=admitted session=01900000-0000-7000-8000-000000000011\n"), std::string::npos)
      << written;
}

TEST(SessionLogContextTest, NoLineAfterTheSessionEndsCarriesASessionId) {
  ASSERT_TRUE(kSession.has_value());
  {
    const SessionLogContext held(*kSession);
  }

  const std::string written = WrittenInfoLine("subsystem=test event=ended");

  EXPECT_EQ(written.find("session="), std::string::npos) << written;
}

}  // namespace

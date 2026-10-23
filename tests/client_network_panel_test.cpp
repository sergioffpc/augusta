#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/correction_tally.h"
#include "augusta/debug_panels.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "network_panel.h"

// The Network Debug panel (ADR-0075): the client's connection, its traffic per
// message type, and its prediction and presentation, built from plain samples
// without drawing anything.
namespace {

using augusta::client::ConnectionReading;
using augusta::client::ConnectionReadings;
using augusta::client::kNetworkPanelId;
using augusta::client::NetworkPanel;
using augusta::client::NetworkSample;
using augusta::debug::Button;
using augusta::debug::ButtonPress;
using augusta::debug::ControlEvent;
using augusta::debug::HistoryPlot;
using augusta::debug::Panel;
using augusta::debug::Slider;
using augusta::debug::Table;
using augusta::debug::Text;
using augusta::link::AuthoritativeStateTally;
using augusta::link::ConnectionStats;
using augusta::link::kMessageTypeCount;
using augusta::link::MessageType;
using augusta::link::NameOf;
using augusta::link::SimulatedConditions;
using augusta::link::SimulatedTraffic;
using augusta::link::Traffic;
using augusta::prediction::Corrections;
using std::chrono::milliseconds;

const NetworkPanel::Clock::time_point kStart{};

ConnectionStats Measured() {
  return {.ping_ms = 42,
          .quality_local = 0.99F,
          .quality_remote = 0.5F,
          .in_bytes_per_sec = 1536.0F,
          .out_bytes_per_sec = 512.0F,
          .max_jitter_us = 7000,
          .pending_bytes = 300};
}

NetworkSample Connected(const ConnectionStats& stats, const Traffic& traffic = {}) {
  NetworkSample sample;
  sample.connection = {.stats = stats, .traffic = traffic};
  return sample;
}

// The Prediction thread's corrections so far, and no connection.
NetworkSample Corrected(const Corrections& corrections) {
  NetworkSample sample;
  sample.corrections = corrections;
  return sample;
}

// Commands sent: messages and their bytes, so far.
Traffic CommandsSent(std::uint64_t messages, std::uint64_t bytes) {
  Traffic traffic;
  traffic.by_type.at(static_cast<std::size_t>(MessageType::kCommands)).sent = {.messages = messages, .bytes = bytes};
  return traffic;
}

constexpr std::uint8_t kTickRateHz = 30;

// Authoritative State updates received so far, the rest of the tally as given.
Traffic UpdatesReceived(std::uint64_t updates, AuthoritativeStateTally tally = {}) {
  Traffic traffic;
  tally.updates = updates;
  traffic.authoritative_state = tally;
  return traffic;
}

// A playing client's sample, admitted by a server ticking at kTickRateHz.
NetworkSample Received(const Traffic& traffic) {
  NetworkSample sample = Connected(Measured(), traffic);
  sample.tick_rate_hz = kTickRateHz;
  return sample;
}

ControlEvent ResetPeaks() {
  return {.panel = std::string(kNetworkPanelId), .control = "reset_peaks", .value = ButtonPress{}};
}

ControlEvent Press(std::string_view button) {
  return {.panel = std::string(kNetworkPanelId), .control = std::string(button), .value = ButtonPress{}};
}

ControlEvent Drag(std::string_view slider, float value) {
  return {.panel = std::string(kNetworkPanelId), .control = std::string(slider), .value = value};
}

std::vector<std::string> Texts(const Panel& panel) {
  std::vector<std::string> texts;
  for (const auto& element : panel.elements) {
    if (const auto* text = std::get_if<Text>(&element)) {
      texts.push_back(text->text);
    }
  }
  return texts;
}

bool HasText(const Panel& panel, std::string_view text) {
  const std::vector<std::string> texts = Texts(panel);
  return std::ranges::find(texts, text) != texts.end();
}

Table TrafficTable(const Panel& panel) {
  for (const auto& element : panel.elements) {
    if (const auto* table = std::get_if<Table>(&element)) {
      return *table;
    }
  }
  ADD_FAILURE() << "the panel has no table";
  return {};
}

std::vector<std::string> RowOf(const Panel& panel, MessageType type) {
  for (const auto& row : TrafficTable(panel).rows) {
    if (!row.empty() && row.front() == NameOf(type)) {
      return row;
    }
  }
  ADD_FAILURE() << "the table has no row for " << NameOf(type);
  return {};
}

HistoryPlot Plot(const Panel& panel, std::string_view label) {
  for (const auto& element : panel.elements) {
    if (const auto* plot = std::get_if<HistoryPlot>(&element); plot != nullptr && plot->label == label) {
      return *plot;
    }
  }
  ADD_FAILURE() << "the panel has no plot " << label;
  return {};
}

template <typename Control>
Control ControlOf(const Panel& panel, std::string_view id) {
  for (const auto& element : panel.elements) {
    if (const auto* control = std::get_if<Control>(&element); control != nullptr && control->id == id) {
      return *control;
    }
  }
  ADD_FAILURE() << "the panel has no control " << id;
  return {};
}

Slider SliderOf(const Panel& panel, std::string_view id) { return ControlOf<Slider>(panel, id); }

Button ButtonOf(const Panel& panel, std::string_view id) { return ControlOf<Button>(panel, id); }

TEST(NetworkPanelTest, IsTheNetworkPanel) {
  const NetworkPanel network;

  const Panel panel = network.BuildPanel();

  EXPECT_EQ(panel.id, kNetworkPanelId);
  EXPECT_EQ(panel.title, "Network");
}

TEST(NetworkPanelTest, TheConnectionGroupShowsTheReading) {
  NetworkPanel network;

  network.Sample(Connected(Measured()), kStart);

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "RTT: 42ms"));
  EXPECT_TRUE(HasText(panel, "Jitter: 7.0ms"));
  EXPECT_TRUE(HasText(panel, "Quality local: 99.0% (worst 99.0%)"));
  EXPECT_TRUE(HasText(panel, "Quality remote: 50.0% (worst 50.0%)"));
  EXPECT_TRUE(HasText(panel, "In: 1.5KB/s"));
  EXPECT_TRUE(HasText(panel, "Out: 512B/s"));
  EXPECT_TRUE(HasText(panel, "Pending: 300B (peak 300B)"));
}

TEST(NetworkPanelTest, AValueNotMeasuredYetReadsDashes) {
  NetworkPanel network;
  EXPECT_TRUE(HasText(network.BuildPanel(), "RTT: --"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: --"));

  network.Sample(Connected({.quality_local = 1.0F, .quality_remote = -1.0F, .max_jitter_us = -1}), kStart);

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Jitter: --"));
  EXPECT_TRUE(HasText(panel, "Quality remote: --"));
  // Control: what is measured reads as itself.
  EXPECT_TRUE(HasText(panel, "Quality local: 100.0% (worst 100.0%)"));
  EXPECT_TRUE(HasText(panel, "RTT: 0ms"));
}

TEST(NetworkPanelTest, NoConnectionReadsDashes) {
  NetworkPanel network;
  network.Sample(Connected(Measured()), kStart);
  ASSERT_TRUE(HasText(network.BuildPanel(), "RTT: 42ms"));

  network.Sample(NetworkSample{}, kStart + milliseconds(100));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "RTT: --"));
  EXPECT_TRUE(HasText(panel, "In: --"));
  EXPECT_TRUE(HasText(panel, "Pending: -- (peak 300B)"));
}

TEST(NetworkPanelTest, PeaksAreKeptUntilReset) {
  NetworkPanel network;
  ConnectionStats busy = Measured();
  busy.pending_bytes = 2048;
  busy.quality_local = 0.9F;
  network.Sample(Connected(busy), kStart);

  network.Sample(Connected(Measured()), kStart + milliseconds(100));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: 300B (peak 2.0KB)"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Quality local: 99.0% (worst 90.0%)"));

  network.Update(ResetPeaks());

  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: 300B (peak 300B)"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Quality local: 99.0% (worst 99.0%)"));
}

TEST(NetworkPanelTest, HasAResetPeaksButton) {
  const NetworkPanel network;

  const Panel panel = network.BuildPanel();

  const auto button = std::ranges::find_if(panel.elements, [](const auto& element) {
    const auto* found = std::get_if<Button>(&element);
    return found != nullptr && found->id == "reset_peaks";
  });
  ASSERT_NE(button, panel.elements.end());
  EXPECT_EQ(std::get<Button>(*button).label, "Reset peaks");
}

TEST(NetworkPanelTest, AnotherPanelsEventResetsNothing) {
  NetworkPanel network;
  ConnectionStats busy = Measured();
  busy.pending_bytes = 2048;
  network.Sample(Connected(busy), kStart);
  network.Sample(Connected(Measured()), kStart + milliseconds(100));

  network.Update({.panel = "profiling", .control = "reset_peaks", .value = ButtonPress{}});
  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: 300B (peak 2.0KB)"));

  // Control: the same event, the Network panel's, resets the peaks.
  network.Update(ResetPeaks());
  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: 300B (peak 300B)"));
}

TEST(NetworkPanelTest, TheTrafficTableHasARowPerMessageTypeZerosIncluded) {
  NetworkPanel network;
  network.Sample(Connected(Measured()), kStart);
  network.Sample(Connected(Measured()), kStart + milliseconds(100));

  const Table table = TrafficTable(network.BuildPanel());

  EXPECT_EQ(table.header, (std::vector<std::string>{"Type", "Msgs/s in", "Bytes/s in", "Msgs/s out", "Bytes/s out"}));
  ASSERT_EQ(table.rows.size(), kMessageTypeCount);
  for (std::size_t i = 0; i < kMessageTypeCount; ++i) {
    EXPECT_EQ(table.rows.at(i), (std::vector<std::string>{std::string(NameOf(static_cast<MessageType>(i))), "0.0 (0.0)",
                                                          "0B/s (0B/s)", "0.0 (0.0)", "0B/s (0B/s)"}));
  }
}

TEST(NetworkPanelTest, TrafficRatesReadDashesUntilTwoSamples) {
  NetworkPanel network;

  network.Sample(Connected(Measured(), CommandsSent(5, 100)), kStart);

  EXPECT_EQ(RowOf(network.BuildPanel(), MessageType::kCommands),
            (std::vector<std::string>{"commands", "--", "--", "--", "--"}));
}

TEST(NetworkPanelTest, TrafficRatesComeFromTheCountsOverTheLastSecond) {
  NetworkPanel network;
  network.Sample(Connected(Measured(), CommandsSent(0, 0)), kStart);
  network.Sample(Connected(Measured(), CommandsSent(30, 300)), kStart + milliseconds(500));
  network.Sample(Connected(Measured(), CommandsSent(90, 900)), kStart + milliseconds(1000));
  // Over the last second only: 60 messages since the sample at 500 ms.
  network.Sample(Connected(Measured(), CommandsSent(90, 900)), kStart + milliseconds(1500));

  EXPECT_EQ(RowOf(network.BuildPanel(), MessageType::kCommands),
            (std::vector<std::string>{"commands", "0.0 (0.0)", "0B/s (0B/s)", "60.0 (90.0)", "600B/s (900B/s)"}));
}

TEST(NetworkPanelTest, TrafficRatesStartOverWhenTheCountsDo) {
  NetworkPanel network;
  network.Sample(Connected(Measured(), CommandsSent(90, 900)), kStart);
  // A new connection counts from zero.
  network.Sample(Connected(Measured(), CommandsSent(10, 100)), kStart + milliseconds(100));
  EXPECT_EQ(RowOf(network.BuildPanel(), MessageType::kCommands).at(3), "--");

  network.Sample(Connected(Measured(), CommandsSent(20, 200)), kStart + milliseconds(200));

  EXPECT_EQ(RowOf(network.BuildPanel(), MessageType::kCommands).at(3), "100.0 (100.0)");
}

TEST(NetworkPanelTest, AuthoritativeStateArrivalsComeFromTheCountsOverTheLastSecond) {
  NetworkPanel network;
  network.Sample(Received(UpdatesReceived(0)), kStart);
  network.Sample(Received(UpdatesReceived(15)), kStart + milliseconds(500));
  network.Sample(Received(UpdatesReceived(40)), kStart + milliseconds(1000));
  // Over the last second only: 25 updates since the sample at 500 ms.
  network.Sample(Received(UpdatesReceived(40)), kStart + milliseconds(1500));

  EXPECT_TRUE(HasText(network.BuildPanel(), "Updates: 25.0/s (peak 40.0/s), tick rate 30Hz"));
}

TEST(NetworkPanelTest, AuthoritativeStateSizesAreTheLastTheAverageAndTheLargestEachWithItsPeak) {
  NetworkPanel network;
  network.Sample(Received(UpdatesReceived(2, {.total_bytes = 4096, .last_bytes = 3072, .largest_bytes = 3072})),
                 kStart);

  network.Sample(Received(UpdatesReceived(4, {.total_bytes = 6144, .last_bytes = 512, .largest_bytes = 3072})),
                 kStart + milliseconds(100));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Last size: 512B (peak 3.0KB)"));
  // Total bytes over updates received: 6144 / 4, after 4096 / 2.
  EXPECT_TRUE(HasText(panel, "Average size: 1.5KB (peak 2.0KB)"));
  EXPECT_TRUE(HasText(panel, "Largest size: 3.0KB (peak 3.0KB)"));
}

TEST(NetworkPanelTest, AuthoritativeStateSizesReadDashesBeforeTheFirstUpdate) {
  NetworkPanel network;

  network.Sample(Received(UpdatesReceived(0)), kStart);

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Last size: --"));
  EXPECT_TRUE(HasText(panel, "Average size: --"));
  EXPECT_TRUE(HasText(panel, "Largest size: --"));
}

TEST(NetworkPanelTest, TicksMissingAndOutOfOrderAreRatesEachWithItsPeak) {
  NetworkPanel network;
  network.Sample(Received(UpdatesReceived(0)), kStart);
  network.Sample(Received(UpdatesReceived(10, {.ticks_missing = 3, .ticks_out_of_order = 1})),
                 kStart + milliseconds(500));
  network.Sample(Received(UpdatesReceived(20, {.ticks_missing = 4, .ticks_out_of_order = 1})),
                 kStart + milliseconds(1000));
  // Over the last second only: one tick missing and none out of order since 500 ms.
  network.Sample(Received(UpdatesReceived(30, {.ticks_missing = 4, .ticks_out_of_order = 1})),
                 kStart + milliseconds(1500));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Ticks missing: 1.0/s (peak 6.0/s)"));
  EXPECT_TRUE(HasText(panel, "Ticks out of order: 0.0/s (peak 2.0/s)"));
}

TEST(NetworkPanelTest, AReplayViewerReadsDashesForTheAuthoritativeStateGroup) {
  NetworkPanel network;
  NetworkSample replay = Received(UpdatesReceived(10, {.total_bytes = 1000, .last_bytes = 100, .ticks_missing = 1}));
  replay.receives_authoritative_state = false;
  network.Sample(replay, kStart);
  replay.connection.traffic = UpdatesReceived(20, {.total_bytes = 2000, .last_bytes = 100, .ticks_missing = 2});

  network.Sample(replay, kStart + milliseconds(100));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Updates: --"));
  EXPECT_TRUE(HasText(panel, "Last size: --"));
  EXPECT_TRUE(HasText(panel, "Average size: --"));
  EXPECT_TRUE(HasText(panel, "Largest size: --"));
  EXPECT_TRUE(HasText(panel, "Ticks missing: --"));
  EXPECT_TRUE(HasText(panel, "Ticks out of order: --"));
  // Control: the Connection group still reads the connection.
  EXPECT_TRUE(HasText(panel, "RTT: 42ms"));
}

TEST(NetworkPanelTest, AuthoritativeStateRatesStartOverWhenTheCountsDo) {
  NetworkPanel network;
  network.Sample(Received(UpdatesReceived(40, {.ticks_missing = 9})), kStart);
  // A new connection counts from zero.
  network.Sample(Received(UpdatesReceived(5)), kStart + milliseconds(100));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Ticks missing: --"));

  network.Sample(Received(UpdatesReceived(8, {.ticks_missing = 1})), kStart + milliseconds(200));

  EXPECT_TRUE(HasText(network.BuildPanel(), "Updates: 30.0/s (peak 30.0/s), tick rate 30Hz"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Ticks missing: 10.0/s (peak 10.0/s)"));
}

TEST(NetworkPanelTest, ATickRateNotKnownYetReadsDashes) {
  NetworkPanel network;
  NetworkSample sample = Received(UpdatesReceived(0));
  sample.tick_rate_hz = std::nullopt;
  network.Sample(sample, kStart);

  network.Sample(sample, kStart + milliseconds(100));

  EXPECT_TRUE(HasText(network.BuildPanel(), "Updates: 0.0/s (peak 0.0/s), tick rate --"));
}

TEST(NetworkPanelTest, AuthoritativeStatePeaksAreKeptUntilReset) {
  NetworkPanel network;
  network.Sample(Received(UpdatesReceived(0)), kStart);
  network.Sample(Received(UpdatesReceived(
                     10, {.total_bytes = 20480, .last_bytes = 4096, .largest_bytes = 4096, .ticks_missing = 5})),
                 kStart + milliseconds(500));
  network.Sample(Received(UpdatesReceived(
                     40, {.total_bytes = 49152, .last_bytes = 1024, .largest_bytes = 4096, .ticks_missing = 5})),
                 kStart + milliseconds(1500));
  ASSERT_TRUE(HasText(network.BuildPanel(), "Last size: 1.0KB (peak 4.0KB)"));

  network.Update(ResetPeaks());

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Updates: 30.0/s (peak 30.0/s), tick rate 30Hz"));
  EXPECT_TRUE(HasText(panel, "Last size: 1.0KB (peak 1.0KB)"));
  EXPECT_TRUE(HasText(panel, "Average size: 1.2KB (peak 1.2KB)"));
  EXPECT_TRUE(HasText(panel, "Largest size: 4.0KB (peak 4.0KB)"));
  EXPECT_TRUE(HasText(panel, "Ticks missing: 0.0/s (peak 0.0/s)"));
  EXPECT_TRUE(HasText(panel, "Ticks out of order: 0.0/s (peak 0.0/s)"));
}

TEST(NetworkPanelTest, PlotsTheConnectionsNumbers) {
  NetworkPanel network;

  network.Sample(Connected(Measured()), kStart);

  const Panel panel = network.BuildPanel();
  EXPECT_EQ(Plot(panel, "RTT (ms)").values, std::vector<float>{42.0F});
  EXPECT_EQ(Plot(panel, "Jitter (ms)").values, std::vector<float>{7.0F});
  EXPECT_EQ(Plot(panel, "Loss (%)").values, std::vector<float>{50.0F});
  EXPECT_EQ(Plot(panel, "In (B/s)").values, std::vector<float>{1536.0F});
  EXPECT_EQ(Plot(panel, "Out (B/s)").values, std::vector<float>{512.0F});
}

TEST(NetworkPanelTest, APlotKeepsTheLastHundredPointsOldestDropped) {
  NetworkPanel network;
  ConnectionStats stats = Measured();
  for (int i = 0; i < 105; ++i) {
    stats.ping_ms = i;
    network.Sample(Connected(stats), kStart + (i * NetworkPanel::kSamplePeriod));
  }

  const std::vector<float> rtt = Plot(network.BuildPanel(), "RTT (ms)").values;

  ASSERT_EQ(rtt.size(), NetworkPanel::kPlotPoints);
  EXPECT_EQ(rtt.front(), 5.0F);
  EXPECT_EQ(rtt.back(), 104.0F);
}

TEST(NetworkPanelTest, ClosingDropsThePlotsAndThePeaks) {
  NetworkPanel network;
  ConnectionStats busy = Measured();
  busy.pending_bytes = 2048;
  network.Sample(Connected(busy), kStart);
  network.Sample(Connected(Measured()), kStart + milliseconds(100));
  ASSERT_EQ(Plot(network.BuildPanel(), "RTT (ms)").values.size(), 2U);

  network.Close();

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(Plot(panel, "RTT (ms)").values.empty());
  EXPECT_TRUE(HasText(panel, "Pending: --"));
  network.Sample(Connected(Measured()), kStart + milliseconds(200));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Pending: 300B (peak 300B)"));
}

TEST(NetworkPanelTest, ASampleIsDueEachFrameThatCrossesAHundredMillisecondBoundary) {
  NetworkPanel network;
  ASSERT_TRUE(network.SampleDue(kStart));
  network.Sample(NetworkSample{}, kStart);

  EXPECT_FALSE(network.SampleDue(kStart + milliseconds(99)));
  EXPECT_TRUE(network.SampleDue(kStart + milliseconds(130)));
  network.Sample(NetworkSample{}, kStart + milliseconds(130));

  // The next boundary is at 200 ms, not 100 ms after the late frame.
  EXPECT_FALSE(network.SampleDue(kStart + milliseconds(199)));
  EXPECT_TRUE(network.SampleDue(kStart + milliseconds(200)));
}

TEST(NetworkPanelTest, ClosingMakesTheNextSampleDue) {
  NetworkPanel network;
  network.Sample(NetworkSample{}, kStart);
  ASSERT_FALSE(network.SampleDue(kStart + milliseconds(50)));

  network.Close();

  EXPECT_TRUE(network.SampleDue(kStart + milliseconds(50)));
}

TEST(NetworkPanelImpairmentTest, WantsTheRealNetworkUntilImpaired) {
  const NetworkPanel network;

  EXPECT_EQ(network.WantedConditions(), SimulatedConditions{});
  EXPECT_FALSE(network.Impaired());
}

// The impaired-link tests' profiles (tools/agent/tests/netcode_test.cpp), there
// applied once each way by both ends in one process, here by the client's
// conditions on what it sends and what it receives.
struct ProfileCase {
  std::string_view button;
  SimulatedConditions conditions;
};

class NetworkPanelProfileTest : public ::testing::TestWithParam<ProfileCase> {};

TEST_P(NetworkPanelProfileTest, AProfileWantsTheImpairedLinkTestsConditionsBothWays) {
  NetworkPanel network;

  network.Update(Press(GetParam().button));

  EXPECT_EQ(network.WantedConditions(), GetParam().conditions);
  EXPECT_TRUE(network.Impaired());
}

INSTANTIATE_TEST_SUITE_P(
    Profiles, NetworkPanelProfileTest,
    ::testing::Values(ProfileCase{.button = "profile_broadband",
                                  .conditions = {.latency_ms = 20,
                                                 .jitter_mean_ms = 5,
                                                 .jitter_max_ms = 20,
                                                 .loss_percent = 1.0F,
                                                 .reorder_percent = 1.0F,
                                                 .reorder_delay_ms = 10,
                                                 .applies_to = SimulatedTraffic::kSentAndReceived}},
                      ProfileCase{.button = "profile_nfr02",
                                  .conditions = {.latency_ms = 50,
                                                 .jitter_mean_ms = 10,
                                                 .jitter_max_ms = 30,
                                                 .loss_percent = 5.0F,
                                                 .reorder_percent = 2.0F,
                                                 .reorder_delay_ms = 20,
                                                 .applies_to = SimulatedTraffic::kSentAndReceived}},
                      ProfileCase{.button = "profile_worst_case",
                                  .conditions = {.latency_ms = 75,
                                                 .jitter_mean_ms = 10,
                                                 .jitter_max_ms = 30,
                                                 .loss_percent = 10.0F,
                                                 .reorder_percent = 5.0F,
                                                 .reorder_delay_ms = 20,
                                                 .applies_to = SimulatedTraffic::kSentAndReceived}}),
    [](const ::testing::TestParamInfo<ProfileCase>& info) { return std::string(info.param.button); });

TEST(NetworkPanelImpairmentTest, OffWantsTheRealNetwork) {
  NetworkPanel network;
  network.Update(Press("profile_worst_case"));
  ASSERT_TRUE(network.Impaired());

  network.Update(Press("impairment_off"));

  EXPECT_EQ(network.WantedConditions(), SimulatedConditions{});
  EXPECT_FALSE(network.Impaired());
}

TEST(NetworkPanelImpairmentTest, EachSliderSetsItsValueBothWays) {
  NetworkPanel network;

  network.Update(Drag("latency_ms", 49.6F));
  network.Update(Drag("jitter_mean_ms", 4.0F));
  network.Update(Drag("jitter_max_ms", 12.0F));
  network.Update(Drag("loss_percent", 2.5F));
  network.Update(Drag("reorder_percent", 3.0F));
  network.Update(Drag("reorder_delay_ms", 15.0F));

  EXPECT_EQ(network.WantedConditions(), (SimulatedConditions{.latency_ms = 50,
                                                             .jitter_mean_ms = 4,
                                                             .jitter_max_ms = 12,
                                                             .loss_percent = 2.5F,
                                                             .reorder_percent = 3.0F,
                                                             .reorder_delay_ms = 15,
                                                             .applies_to = SimulatedTraffic::kSentAndReceived}));
  EXPECT_TRUE(network.Impaired());
}

TEST(NetworkPanelImpairmentTest, ASliderMovedWhileAProfileIsAppliedChangesOnlyThatValue) {
  NetworkPanel network;
  network.Update(Press("profile_nfr02"));

  network.Update(Drag("latency_ms", 100.0F));

  EXPECT_EQ(network.WantedConditions(), (SimulatedConditions{.latency_ms = 100,
                                                             .jitter_mean_ms = 10,
                                                             .jitter_max_ms = 30,
                                                             .loss_percent = 5.0F,
                                                             .reorder_percent = 2.0F,
                                                             .reorder_delay_ms = 20,
                                                             .applies_to = SimulatedTraffic::kSentAndReceived}));
}

TEST(NetworkPanelImpairmentTest, EverySliderBackAtZeroWantsTheRealNetwork) {
  NetworkPanel network;
  network.Update(Drag("latency_ms", 30.0F));
  network.Update(Drag("loss_percent", 1.0F));
  ASSERT_TRUE(network.Impaired());

  network.Update(Drag("latency_ms", 0.0F));
  EXPECT_TRUE(network.Impaired());
  network.Update(Drag("loss_percent", 0.0F));

  EXPECT_EQ(network.WantedConditions(), SimulatedConditions{});
  EXPECT_FALSE(network.Impaired());
}

TEST(NetworkPanelImpairmentTest, ASliderKeepsWithinItsRange) {
  NetworkPanel network;

  network.Update(Drag("latency_ms", 400.0F));
  network.Update(Drag("loss_percent", -5.0F));

  EXPECT_EQ(network.WantedConditions().latency_ms, 250);
  EXPECT_EQ(network.WantedConditions().loss_percent, 0.0F);
}

TEST(NetworkPanelImpairmentTest, TheSlidersShowTheWantedValuesWithinTheirRanges) {
  NetworkPanel network;
  network.Update(Press("profile_broadband"));

  const Panel panel = network.BuildPanel();

  const Slider latency = SliderOf(panel, "latency_ms");
  EXPECT_EQ(latency.label, "Latency, each way (ms)");
  EXPECT_EQ(latency.value, 20.0F);
  EXPECT_EQ(latency.min, 0.0F);
  EXPECT_EQ(latency.max, 250.0F);
  EXPECT_EQ(SliderOf(panel, "jitter_mean_ms").value, 5.0F);
  EXPECT_EQ(SliderOf(panel, "jitter_mean_ms").max, 100.0F);
  EXPECT_EQ(SliderOf(panel, "jitter_max_ms").value, 20.0F);
  EXPECT_EQ(SliderOf(panel, "jitter_max_ms").max, 100.0F);
  EXPECT_EQ(SliderOf(panel, "loss_percent").value, 1.0F);
  EXPECT_EQ(SliderOf(panel, "loss_percent").max, 50.0F);
  EXPECT_EQ(SliderOf(panel, "reorder_percent").value, 1.0F);
  EXPECT_EQ(SliderOf(panel, "reorder_percent").max, 50.0F);
  EXPECT_EQ(SliderOf(panel, "reorder_delay_ms").value, 10.0F);
  EXPECT_EQ(SliderOf(panel, "reorder_delay_ms").max, 100.0F);
}

TEST(NetworkPanelImpairmentTest, HasAButtonPerProfileAndOff) {
  const NetworkPanel network;

  const Panel panel = network.BuildPanel();

  EXPECT_EQ(ButtonOf(panel, "profile_broadband").label, "Broadband");
  EXPECT_EQ(ButtonOf(panel, "profile_nfr02").label, "NFR-02");
  EXPECT_EQ(ButtonOf(panel, "profile_worst_case").label, "Worst case");
  EXPECT_EQ(ButtonOf(panel, "impairment_off").label, "Off");
}

TEST(NetworkPanelImpairmentTest, TheGroupShowsTheMeasuredRtt) {
  NetworkPanel network;
  EXPECT_TRUE(HasText(network.BuildPanel(), "Measured RTT: --"));

  network.Sample(Connected(Measured()), kStart);

  EXPECT_TRUE(HasText(network.BuildPanel(), "Measured RTT: 42ms"));
}

TEST(NetworkPanelImpairmentTest, AnotherPanelsEventImpairsNothing) {
  NetworkPanel network;

  network.Update({.panel = "profiling", .control = "profile_worst_case", .value = ButtonPress{}});
  EXPECT_FALSE(network.Impaired());

  // Control: the same event, the Network panel's, impairs the link.
  network.Update(Press("profile_worst_case"));
  EXPECT_TRUE(network.Impaired());
}

TEST(NetworkPanelImpairmentTest, OutlivesClosingThePanel) {
  NetworkPanel network;
  network.Update(Press("profile_nfr02"));

  network.Close();

  EXPECT_TRUE(network.Impaired());
  EXPECT_EQ(network.WantedConditions().latency_ms, 50);
  EXPECT_EQ(SliderOf(network.BuildPanel(), "latency_ms").value, 50.0F);
}

TEST(NetworkPanelTest, CorrectionRatesComeFromTheCountsOverTheLastSecond) {
  NetworkPanel network;
  network.Sample(Corrected({.count = 0, .rifle = 0}), kStart);
  network.Sample(Corrected({.count = 3, .rifle = 1}), kStart + milliseconds(500));
  network.Sample(Corrected({.count = 9, .rifle = 4}), kStart + milliseconds(1000));
  // Over the last second only: 6 corrections and 3 rifle ones since 500 ms.
  network.Sample(Corrected({.count = 9, .rifle = 4}), kStart + milliseconds(1500));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Corrections/s: 6.0 (peak 9.0)"));
  EXPECT_TRUE(HasText(panel, "Rifle corrections/s: 3.0 (peak 4.0)"));
}

TEST(NetworkPanelTest, TheCorrectionSizeIsTheLastWithTheLargestSinceTheReset) {
  NetworkPanel network;
  network.Sample(Corrected({.count = 1, .last_m = 0.4F, .largest_m = 0.4F}), kStart);
  network.Sample(Corrected({.count = 2, .last_m = 0.25F, .largest_m = 0.4F}), kStart + milliseconds(100));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Correction: 0.25m (largest 0.40m)"));

  network.Update(ResetPeaks());
  EXPECT_TRUE(HasText(network.BuildPanel(), "Correction: 0.25m (largest 0.25m)"));

  // A larger one between two samples, not the last, is still the largest.
  network.Sample(Corrected({.count = 4, .last_m = 0.1F, .largest_m = 0.5F}), kStart + milliseconds(200));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Correction: 0.10m (largest 0.50m)"));
}

TEST(NetworkPanelTest, NoPredictionStateReadsDashesForTheCorrections) {
  NetworkPanel network;
  network.Sample(Connected(Measured()), kStart);
  network.Sample(Connected(Measured()), kStart + milliseconds(100));

  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Corrections/s: --"));
  EXPECT_TRUE(HasText(panel, "Correction: --"));
  EXPECT_TRUE(HasText(panel, "Rifle corrections/s: --"));
  EXPECT_TRUE(Plot(panel, "Correction (m)").values.empty());
}

TEST(NetworkPanelTest, CorrectionSizeIsPlottedAsTheLargestOfEachSampleOrZero) {
  NetworkPanel network;
  network.Sample(Corrected({.count = 0}), kStart);
  network.Sample(Corrected({.count = 1, .last_m = 0.3F, .largest_m = 0.3F}), kStart + milliseconds(100));
  network.Sample(Corrected({.count = 1, .last_m = 0.3F, .largest_m = 0.3F}), kStart + milliseconds(200));
  // Two in one sample, the smaller last: the larger is plotted.
  network.Sample(Corrected({.count = 3, .last_m = 0.1F, .largest_m = 0.5F}), kStart + milliseconds(300));
  // Two in one sample, neither the largest so far: the last stands for them.
  network.Sample(Corrected({.count = 5, .last_m = 0.2F, .largest_m = 0.5F}), kStart + milliseconds(400));

  EXPECT_EQ(Plot(network.BuildPanel(), "Correction (m)").values, (std::vector<float>{0.0F, 0.3F, 0.0F, 0.5F, 0.2F}));
}

TEST(NetworkPanelTest, TheCorrectionPlotKeepsTheLastHundredPointsOldestDropped) {
  NetworkPanel network;
  for (std::uint32_t i = 0; i < 105; ++i) {
    const float size = static_cast<float>(i + 1) / 1000.0F;
    network.Sample(Corrected({.count = i + 1, .last_m = size, .largest_m = size}),
                   kStart + (static_cast<int>(i) * NetworkPanel::kSamplePeriod));
  }

  const std::vector<float> sizes = Plot(network.BuildPanel(), "Correction (m)").values;

  ASSERT_EQ(sizes.size(), NetworkPanel::kPlotPoints);
  EXPECT_FLOAT_EQ(sizes.front(), 0.006F);
  EXPECT_FLOAT_EQ(sizes.back(), 0.105F);
}

TEST(NetworkPanelTest, TheSlackIsTheSmallestOverTheLastSecondBesideTheInterpolationDelay) {
  NetworkPanel network;
  network.Sample({}, kStart);
  network.RecordFrame(0.010);
  network.Sample({}, kStart + milliseconds(500));
  network.RecordFrame(0.060);
  network.RecordFrame(0.040);
  network.Sample({}, kStart + milliseconds(1000));
  network.RecordFrame(0.080);
  // The 10 ms frame was before the last second.
  network.Sample({}, kStart + milliseconds(1500));

  EXPECT_TRUE(HasText(network.BuildPanel(), "Slack: 40.0ms (worst 10.0ms), delay 100ms"));
}

TEST(NetworkPanelTest, DryFramesAreTheFramesPerSecondWhoseSlackWasZeroOrBelow) {
  NetworkPanel network;
  network.Sample({}, kStart);
  network.RecordFrame(0.0);
  network.RecordFrame(-0.020);
  network.RecordFrame(0.030);
  network.Sample({}, kStart + milliseconds(500));

  EXPECT_TRUE(HasText(network.BuildPanel(), "Dry frames/s: 4.0 (peak 4.0)"));
}

TEST(NetworkPanelTest, NoSlackReadsDashesForTheInterpolation) {
  NetworkPanel network;
  network.Sample({}, kStart);
  network.RecordFrame(std::nullopt);
  network.Sample({}, kStart + milliseconds(100));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Slack: --, delay 100ms"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Dry frames/s: --"));

  // Control: a frame with a slack reads as itself.
  network.RecordFrame(0.050);
  network.Sample({}, kStart + milliseconds(200));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Slack: 50.0ms (worst 50.0ms), delay 100ms"));
  EXPECT_TRUE(HasText(network.BuildPanel(), "Dry frames/s: 0.0 (peak 0.0)"));
}

TEST(NetworkPanelTest, ClosingDropsTheCorrectionsAndTheFramesNotSampledYet) {
  NetworkPanel network;
  network.Sample(Corrected({.count = 1, .last_m = 0.3F, .largest_m = 0.3F}), kStart);
  network.RecordFrame(0.0);

  network.Close();

  network.Sample({}, kStart + milliseconds(100));
  network.RecordFrame(0.050);
  network.Sample({}, kStart + milliseconds(200));
  const Panel panel = network.BuildPanel();
  EXPECT_TRUE(HasText(panel, "Correction: --"));
  EXPECT_TRUE(Plot(panel, "Correction (m)").values.empty());
  EXPECT_TRUE(HasText(panel, "Dry frames/s: 0.0 (peak 0.0)"));
}

TEST(ConnectionReadingsTest, TakesTheNewestReadingWithThePeakJitterSinceTheLastTake) {
  ConnectionReadings readings;
  ConnectionStats stats = Measured();
  stats.max_jitter_us = 3000;
  readings.Record(stats, CommandsSent(1, 10));
  stats.max_jitter_us = 7000;
  readings.Record(stats, CommandsSent(2, 20));
  stats.max_jitter_us = 1000;
  stats.ping_ms = 50;
  readings.Record(stats, CommandsSent(3, 30));

  const ConnectionReading reading = readings.Take();

  ASSERT_TRUE(reading.stats.has_value());
  EXPECT_EQ(reading.stats->ping_ms, 50);
  EXPECT_EQ(reading.stats->max_jitter_us, 7000);
  EXPECT_EQ(reading.traffic, CommandsSent(3, 30));
}

TEST(ConnectionReadingsTest, ATakeStartsTheJitterPeakOver) {
  ConnectionReadings readings;
  ConnectionStats stats = Measured();
  stats.max_jitter_us = 7000;
  readings.Record(stats, {});
  ASSERT_EQ(readings.Take().stats->max_jitter_us, 7000);

  EXPECT_EQ(readings.Take().stats->max_jitter_us, -1);
  stats.max_jitter_us = 2000;
  readings.Record(stats, {});
  EXPECT_EQ(readings.Take().stats->max_jitter_us, 2000);
}

TEST(ConnectionReadingsTest, NoConnectionForgetsTheJitterPeak) {
  ConnectionReadings readings;
  ConnectionStats stats = Measured();
  stats.max_jitter_us = 7000;
  readings.Record(stats, {});

  readings.Record(std::nullopt, {});
  EXPECT_FALSE(readings.Take().stats.has_value());
  stats.max_jitter_us = 2000;
  readings.Record(stats, {});

  EXPECT_EQ(readings.Take().stats->max_jitter_us, 2000);
}

}  // namespace

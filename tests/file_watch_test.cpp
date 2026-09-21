#include "file_watch.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

// The decision (ChangeDetector) is tested with times of its own; the mechanism
// (FileWatcher) with real files and short intervals (ADR-0039).
namespace {

using augusta::server::ChangeDetector;
using augusta::server::FileSignature;
using augusta::server::FileWatcher;
using augusta::server::Look;
using augusta::server::WatchEvent;
using augusta::server::WatchOptions;
using namespace std::chrono_literals;

constexpr auto kDebounce = 100ms;
const auto kStart = std::chrono::steady_clock::time_point{} + 1h;

FileSignature Signature(const char* target, int seconds, std::uintmax_t size) {
  return FileSignature{
      .target = target, .modified = std::filesystem::file_time_type{} + std::chrono::seconds(seconds), .size = size};
}

TEST(ChangeDetectorTest, NothingIsReportedWhileTheFileIsUnchanged) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(detector.Feed(Signature("a", 1, 10), kStart + i * 50ms), WatchEvent::kNone);
  }
}

TEST(ChangeDetectorTest, AChangeIsReportedOnceItHasStoppedChangingForTheDebounceTime) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + 50ms), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + kDebounce), WatchEvent::kChanged);
}

TEST(ChangeDetectorTest, TheSameChangeIsNotReportedTwice) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);
  detector.Feed(Signature("a", 2, 12), kStart);
  ASSERT_EQ(detector.Feed(Signature("a", 2, 12), kStart + kDebounce), WatchEvent::kChanged);

  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + 2 * kDebounce), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + 10 * kDebounce), WatchEvent::kNone);
}

TEST(ChangeDetectorTest, ABurstOfChangesIsOneReportOnceItSettles) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  // Each write is within the debounce time of the last: none of them settles.
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(detector.Feed(Signature("a", 2 + i, 12 + i), kStart + i * 40ms), WatchEvent::kNone) << i;
  }

  EXPECT_EQ(detector.Feed(Signature("a", 7, 17), kStart + 5 * 40ms + kDebounce), WatchEvent::kChanged);
  EXPECT_EQ(detector.Feed(Signature("a", 7, 17), kStart + 5 * 40ms + 2 * kDebounce), WatchEvent::kNone);
}

TEST(ChangeDetectorTest, AChangeUndoneBeforeItSettlesIsNotReported) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 1, 10), kStart + 40ms), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 1, 10), kStart + 10 * kDebounce), WatchEvent::kNone);
}

TEST(ChangeDetectorTest, AFileThatNowResolvesElsewhereIsAChangeThoughItLooksTheSame) {
  // A mounted ConfigMap swaps a link to a new directory holding a file with the same size and time.
  ChangeDetector detector(Signature("v1/params.lua", 1, 10), kDebounce);

  detector.Feed(Signature("v2/params.lua", 1, 10), kStart);

  EXPECT_EQ(detector.Feed(Signature("v2/params.lua", 1, 10), kStart + kDebounce), WatchEvent::kChanged);
}

TEST(ChangeDetectorTest, AFileThatCannotBeObservedIsLostOnceAndAChangeWhenItReturns) {
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  EXPECT_EQ(detector.Feed(std::nullopt, kStart), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(std::nullopt, kStart + kDebounce), WatchEvent::kLost);
  EXPECT_EQ(detector.Feed(std::nullopt, kStart + 5 * kDebounce), WatchEvent::kNone);

  EXPECT_EQ(detector.Feed(Signature("a", 3, 11), kStart + 6 * kDebounce), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 3, 11), kStart + 7 * kDebounce), WatchEvent::kChanged);
}

TEST(ChangeDetectorTest, AFileMissingOnlyBrieflyIsNotLost) {
  // An editor that saves by deleting and writing again.
  ChangeDetector detector(Signature("a", 1, 10), kDebounce);

  EXPECT_EQ(detector.Feed(std::nullopt, kStart), WatchEvent::kNone);
  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + 30ms), WatchEvent::kNone);

  EXPECT_EQ(detector.Feed(Signature("a", 2, 12), kStart + 30ms + kDebounce), WatchEvent::kChanged);
}

// A directory of its own per test, so tests run in parallel never share a file.
class FileWatcherTest : public ::testing::Test {
 protected:
  static constexpr WatchOptions kOptions{.poll_interval = 5ms, .debounce = 40ms};

  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() / ("augusta_watch_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory_);
    file_ = directory_ / "parameters.lua";
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  static void Write(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
  }

  // Waits up to a generous few seconds (a sanitizer build is slow) for count to reach n.
  static bool WaitForCount(const std::atomic<int>& count, int n) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (count.load() < n && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(5ms);
    }
    return count.load() >= n;
  }

  // How long to look for a callback that should not come, many debounce times.
  static void WaitOutAnExtraCallback() { std::this_thread::sleep_for(400ms); }

  std::filesystem::path directory_;
  std::filesystem::path file_;
};

TEST_F(FileWatcherTest, TheFileAsItIsAtConstructionIsNotAChange) {
  Write(file_, "one");
  std::atomic<int> changes{0};

  const FileWatcher watcher(file_, kOptions, [&] { ++changes; });

  WaitOutAnExtraCallback();
  EXPECT_EQ(changes.load(), 0);
}

TEST_F(FileWatcherTest, SavingTheFileCallsBackOnce) {
  Write(file_, "one");
  std::atomic<int> changes{0};
  const FileWatcher watcher(file_, kOptions, [&] { ++changes; });

  Write(file_, "one and more");

  ASSERT_TRUE(WaitForCount(changes, 1));
  WaitOutAnExtraCallback();
  EXPECT_EQ(changes.load(), 1);
}

TEST_F(FileWatcherTest, ABurstOfSavesCallsBackOnce) {
  Write(file_, "one");
  std::atomic<int> changes{0};
  const FileWatcher watcher(file_, kOptions, [&] { ++changes; });

  for (int i = 1; i <= 6; ++i) {
    Write(file_, std::string(static_cast<std::size_t>(3 + i), 'x'));
    std::this_thread::sleep_for(3ms);
  }

  ASSERT_TRUE(WaitForCount(changes, 1));
  WaitOutAnExtraCallback();
  EXPECT_EQ(changes.load(), 1);
}

TEST_F(FileWatcherTest, ReplacingTheFileWithAnotherCallsBack) {
  Write(file_, "one");
  std::atomic<int> changes{0};
  const FileWatcher watcher(file_, kOptions, [&] { ++changes; });

  const auto replacement = directory_ / "parameters.lua.new";
  Write(replacement, "another script");
  std::filesystem::rename(replacement, file_);

  EXPECT_TRUE(WaitForCount(changes, 1));
}

TEST_F(FileWatcherTest, SwappingALinkToAnotherDirectoryCallsBackThoughTheFileLooksTheSame) {
  // What a mounted ConfigMap does: `data` is a link to a directory of the
  // current version, and an update points it at a new one.
  const auto v1 = directory_ / "v1";
  const auto v2 = directory_ / "v2";
  std::filesystem::create_directories(v1);
  std::filesystem::create_directories(v2);
  Write(v1 / "parameters.lua", "same size");
  Write(v2 / "parameters.lua", "same size");
  std::filesystem::last_write_time(v2 / "parameters.lua", std::filesystem::last_write_time(v1 / "parameters.lua"));
  const auto data = directory_ / "data";
  std::error_code error;
  std::filesystem::create_directory_symlink(v1, data, error);
  if (error) {
    GTEST_SKIP() << "cannot create a symbolic link here: " << error.message();
  }
  std::atomic<int> changes{0};
  const FileWatcher watcher(data / "parameters.lua", kOptions, [&] { ++changes; });

  const auto swap = directory_ / "data.swap";
  std::filesystem::create_directory_symlink(v2, swap, error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::rename(swap, data, error);
  if (error) {
    GTEST_SKIP() << "cannot swap a link here: " << error.message();
  }

  EXPECT_TRUE(WaitForCount(changes, 1));
}

TEST_F(FileWatcherTest, AFileThatVanishesKeepsTheWatcherRunningAndCallsBackWhenItReturns) {
  Write(file_, "one");
  std::atomic<int> changes{0};
  const FileWatcher watcher(file_, kOptions, [&] { ++changes; });

  std::filesystem::remove(file_);
  WaitOutAnExtraCallback();
  EXPECT_EQ(changes.load(), 0);
  Write(file_, "back again");

  EXPECT_TRUE(WaitForCount(changes, 1));
}

TEST_F(FileWatcherTest, TheWatcherStopsPromptlyEvenWithALongPollInterval) {
  Write(file_, "one");
  const auto begun = std::chrono::steady_clock::now();

  {
    const FileWatcher watcher(file_, WatchOptions{.poll_interval = 60s, .debounce = 40ms}, [] {});
  }

  EXPECT_LT(std::chrono::steady_clock::now() - begun, 5s);
}

TEST(LookTest, AMissingFileCannotBeObserved) {
  EXPECT_FALSE(Look(std::filesystem::temp_directory_path() / "augusta_watch_no_such_file.lua").has_value());
}

}  // namespace

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

// Blocks freed on another thread than the one that allocated them, which the
// hot-path benchmarks never do and the candidates differ most at (#1351): a
// producer allocates batches of blocks of mixed sizes, as a job's results are,
// and hands each to a thread of its own that frees them, as whoever consumes
// them would. Only the allocator benchmarks run it, not augusta_benchmarks,
// whose history the nightly keeps (ADR-0013).
namespace {

constexpr std::size_t kBatchBlocks = 256;
// How many batches wait to be freed before the producer waits in turn, which
// bounds what the run holds.
constexpr std::size_t kMaxWaitingBatches = 8;

// The batch's block sizes, from 16 bytes to 4 KiB, the same every run.
std::array<std::size_t, kBatchBlocks> BlockSizes() {
  constexpr std::uint_fast32_t kSeed = 1351;
  constexpr std::size_t kMinBytes = 16;
  constexpr std::size_t kMaxBytes = 4096;
  std::minstd_rand generator(kSeed);
  std::uniform_int_distribution<std::size_t> bytes(kMinBytes, kMaxBytes);
  std::array<std::size_t, kBatchBlocks> sizes{};
  for (auto& size : sizes) {
    size = bytes(generator);
  }
  return sizes;
}

// The thread that frees the batches handed to it.
class Freer {
 public:
  Freer() : thread_([this](const std::stop_token& stop) { Run(stop); }) {}

  // Waits while kMaxWaitingBatches are already waiting.
  void Hand(std::vector<void*> batch) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return waiting_.size() < kMaxWaitingBatches; });
    waiting_.push_back(std::move(batch));
    changed_.notify_all();
  }

  // Waits until every batch handed is freed.
  void Drain() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return waiting_.empty() && !freeing_; });
  }

 private:
  void Run(const std::stop_token& stop) {
    std::unique_lock lock(mutex_);
    while (changed_.wait(lock, stop, [this] { return !waiting_.empty(); })) {
      const std::vector<void*> batch = std::move(waiting_.front());
      waiting_.pop_front();
      freeing_ = true;
      changed_.notify_all();
      lock.unlock();
      for (void* block : batch) {
        ::operator delete(block);
      }
      lock.lock();
      freeing_ = false;
      changed_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable_any changed_;
  std::deque<std::vector<void*>> waiting_;
  bool freeing_ = false;
  // Last: it starts once the rest is in place, and stops before it goes.
  std::jthread thread_;
};

// Blocks allocated per second, and freed on the Freer's thread; with more
// benchmark threads, each with a Freer of its own, so their allocations
// contend too.
void BM_CrossThreadChurn(benchmark::State& state) {
  const auto sizes = BlockSizes();
  Freer freer;
  for (auto _ : state) {
    std::vector<void*> batch;
    batch.reserve(kBatchBlocks);
    for (const std::size_t size : sizes) {
      void* block = ::operator new(size);
      // Touched, so its page is resident, as a block in use is.
      *static_cast<std::byte*>(block) = std::byte{1};
      batch.push_back(block);
    }
    freer.Hand(std::move(batch));
  }
  freer.Drain();
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatchBlocks));
}
BENCHMARK(BM_CrossThreadChurn)->ThreadRange(1, 4)->UseRealTime();

}  // namespace

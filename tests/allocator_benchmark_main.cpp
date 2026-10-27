#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <print>
#include <string>

#include <benchmark/benchmark.h>

#include "allocator_candidate.h"

#ifdef _WIN32
#include <windows.h>
// psapi.h needs the types windows.h declares, so it comes after it, unsorted.
#include <psapi.h>
#elifdef __APPLE__
#include <mach/mach.h>
#else
#include <fstream>
#include <string_view>
#endif

// The main of every allocator benchmark executable (#1351): it refuses to run
// when its candidate does not serve the process's allocations, which would
// measure the platform's allocator under the candidate's name, then runs the
// benchmarks with the process's resident set reported beside each.
namespace {

using augusta::benchmarks::CandidateServed;
using augusta::benchmarks::kCandidate;

// The process's resident set, in bytes: now, and the most it has been. No
// value when the system does not tell, which reports none rather than 0.
struct Resident {
  std::int64_t bytes = 0;
  std::int64_t peak_bytes = 0;
};

#ifdef _WIN32
std::optional<Resident> ResidentNow() {
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return std::nullopt;
  }
  return Resident{.bytes = static_cast<std::int64_t>(counters.WorkingSetSize),
                  .peak_bytes = static_cast<std::int64_t>(counters.PeakWorkingSetSize)};
}
#elifdef __APPLE__
std::optional<Resident> ResidentNow() {
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return std::nullopt;
  }
  return Resident{.bytes = static_cast<std::int64_t>(info.resident_size),
                  .peak_bytes = static_cast<std::int64_t>(info.resident_size_max)};
}
#else
// /proc/self/status's VmRSS and VmHWM, which it gives in kB.
std::optional<Resident> ResidentNow() {
  constexpr std::string_view kNow = "VmRSS:";
  constexpr std::string_view kPeak = "VmHWM:";
  constexpr std::int64_t kKilobyte = 1024;
  Resident resident;
  std::ifstream status("/proc/self/status");
  if (!status) {
    return std::nullopt;
  }
  for (std::string line; std::getline(status, line);) {
    if (line.starts_with(kNow)) {
      resident.bytes = std::stoll(line.substr(kNow.size())) * kKilobyte;
    } else if (line.starts_with(kPeak)) {
      resident.peak_bytes = std::stoll(line.substr(kPeak.size())) * kKilobyte;
    }
  }
  return resident;
}
#endif

// Google Benchmark's memory measurement, run apart from the timed runs, given
// the resident set instead of the heap it is named for: what fragmentation
// shows in. max_bytes_used holds the process's peak so far, so the last
// benchmark's is the whole run's; net_heap_growth what the benchmark left
// resident, its steady state. Neither, when the system does not tell.
class ResidentMeter final : public benchmark::MemoryManager {
 public:
  void Start() override { start_ = ResidentNow(); }

  void Stop(Result& result) override {
    const std::optional<Resident> now = ResidentNow();
    if (!start_ || !now) {
      return;
    }
    result.max_bytes_used = now->peak_bytes;
    result.net_heap_growth = now->bytes - start_->bytes;
  }

 private:
  std::optional<Resident> start_;
};

// Whether the candidate serves both malloc and operator new.
bool CandidateServesProcess() {
  constexpr std::size_t kProbeBytes = 64;
  void* c_block = std::malloc(kProbeBytes);
  void* cpp_block = ::operator new(kProbeBytes);
  benchmark::DoNotOptimize(c_block);
  benchmark::DoNotOptimize(cpp_block);
  const bool served = CandidateServed(c_block) && CandidateServed(cpp_block);
  ::operator delete(cpp_block);
  std::free(c_block);
  return served;
}

}  // namespace

int main(int argc, char** argv) {
  if (!CandidateServesProcess()) {
    std::println(stderr, "{} does not serve this process's allocations: its results would be the platform's",
                 kCandidate);
    return EXIT_FAILURE;
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return EXIT_FAILURE;
  }
  benchmark::AddCustomContext("allocator", std::string(kCandidate));
  ResidentMeter meter;
  benchmark::RegisterMemoryManager(&meter);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::RegisterMemoryManager(nullptr);
  benchmark::Shutdown();
  return EXIT_SUCCESS;
}

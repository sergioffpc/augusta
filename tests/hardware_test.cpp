#include "augusta/hardware.h"

#include <string>

#include <gtest/gtest.h>

#include "proc_files.h"

namespace augusta::hardware {
namespace {

TEST(CpuModelFromCpuinfo, ReadsTheFirstModelName) {
  const std::string cpuinfo =
      "processor\t: 0\n"
      "model\t\t: 97\n"
      "model name\t: AMD Ryzen 9 7950X 16-Core Processor  \n"
      "processor\t: 1\n"
      "model name\t: Something Else\n";
  EXPECT_EQ(CpuModelFromCpuinfo(cpuinfo), "AMD Ryzen 9 7950X 16-Core Processor");
}

TEST(CpuModelFromCpuinfo, IsEmptyWithoutOne) { EXPECT_EQ(CpuModelFromCpuinfo("processor\t: 0\nmodel\t\t: 97\n"), ""); }

TEST(MemoryFromMeminfo, ReadsTotalAndAvailableInBytes) {
  const Memory memory = MemoryFromMeminfo(
      "MemTotal:       32768 kB\n"
      "MemFree:         1024 kB\n"
      "MemAvailable:   16384 kB\n");
  EXPECT_EQ(memory.total_bytes, 32768ULL * 1024);
  EXPECT_EQ(memory.available_bytes, 16384ULL * 1024);
}

TEST(MemoryFromMeminfo, IsZeroForWhatItLacks) {
  const Memory memory = MemoryFromMeminfo("MemTotal:       oops\n");
  EXPECT_EQ(memory.total_bytes, 0U);
  EXPECT_EQ(memory.available_bytes, 0U);
}

TEST(PrettyNameFromOsRelease, Unquotes) {
  EXPECT_EQ(PrettyNameFromOsRelease("NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 24.04.1 LTS\"\nID=ubuntu\n"),
            "Ubuntu 24.04.1 LTS");
  EXPECT_EQ(PrettyNameFromOsRelease("PRETTY_NAME=Alpine\n"), "Alpine");
  EXPECT_EQ(PrettyNameFromOsRelease("NAME=Ubuntu\n"), "");
}

TEST(DescribeMachine, WritesEachField) {
  const Machine machine{.os = "Windows 10.0.26100 (24H2)",
                        .cpu = {.model = "AMD Ryzen 9 7950X", .logical_cores = 32},
                        .memory = {.total_bytes = 64ULL << 30, .available_bytes = 40ULL << 30}};
  EXPECT_EQ(DescribeMachine(machine),
            "os=\"Windows 10.0.26100 (24H2)\" cpu=\"AMD Ryzen 9 7950X\" logical_cores=32 memory_mib=65536 "
            "memory_available_mib=40960");
}

TEST(DescribeMachine, SaysUnknownForWhatTheOsDidNotTell) {
  EXPECT_EQ(DescribeMachine(Machine{}),
            "os=unknown cpu=unknown logical_cores=unknown memory_mib=unknown memory_available_mib=unknown");
}

// The machine running the test reports the fields every supported OS has.
TEST(QueryMachine, FindsTheOsCoresAndMemory) {
  const Machine machine = QueryMachine();
  EXPECT_FALSE(machine.os.empty());
  EXPECT_GT(machine.cpu.logical_cores, 0U);
  EXPECT_GT(machine.memory.total_bytes, 0U);
}

}  // namespace
}  // namespace augusta::hardware

#include "augusta/hardware.h"

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "proc_files.h"

namespace augusta::hardware {
namespace {

// Requirements: NFR-19
TEST(CpuModelFromCpuinfo, ReadsTheFirstModelName) {
  const std::string cpuinfo =
      "processor\t: 0\n"
      "model\t\t: 97\n"
      "model name\t: AMD Ryzen 9 7950X 16-Core Processor  \n"
      "processor\t: 1\n"
      "model name\t: Something Else\n";
  EXPECT_EQ(CpuModelFromCpuinfo(cpuinfo), "AMD Ryzen 9 7950X 16-Core Processor");
}

// Requirements: NFR-19
TEST(CpuModelFromCpuinfo, IsEmptyWithoutOne) { EXPECT_EQ(CpuModelFromCpuinfo("processor\t: 0\nmodel\t\t: 97\n"), ""); }

// Requirements: NFR-19
TEST(MemoryFromMeminfo, ReadsTotalAndAvailableInBytes) {
  const Memory memory = MemoryFromMeminfo(
      "MemTotal:       32768 kB\n"
      "MemFree:         1024 kB\n"
      "MemAvailable:   16384 kB\n");
  EXPECT_EQ(memory.total_bytes, 32768ULL * 1024);
  EXPECT_EQ(memory.available_bytes, 16384ULL * 1024);
}

// Requirements: NFR-19
TEST(MemoryFromMeminfo, IsZeroForWhatItLacks) {
  const Memory memory = MemoryFromMeminfo("MemTotal:       oops\n");
  EXPECT_EQ(memory.total_bytes, 0U);
  EXPECT_EQ(memory.available_bytes, 0U);
}

// Requirements: NFR-19
TEST(PrettyNameFromOsRelease, Unquotes) {
  EXPECT_EQ(PrettyNameFromOsRelease("NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 24.04.1 LTS\"\nID=ubuntu\n"),
            "Ubuntu 24.04.1 LTS");
  EXPECT_EQ(PrettyNameFromOsRelease("PRETTY_NAME=Alpine\n"), "Alpine");
  EXPECT_EQ(PrettyNameFromOsRelease("NAME=Ubuntu\n"), "");
}

// Requirements: NFR-19
TEST(CpusFromCpuMax, DividesQuotaByPeriod) {
  EXPECT_EQ(CpusFromCpuMax("250000 100000\n"), 2.5);
  EXPECT_EQ(CpusFromCpuMax("max 100000\n"), std::nullopt);
  EXPECT_EQ(CpusFromCpuMax(""), std::nullopt);
  EXPECT_EQ(CpusFromCpuMax("100000 0\n"), std::nullopt);
}

// Requirements: NFR-19
TEST(BytesFromMemoryMax, ReadsANumber) {
  EXPECT_EQ(BytesFromMemoryMax("4294967296\n"), std::optional<std::uint64_t>{4294967296ULL});
  EXPECT_EQ(BytesFromMemoryMax("max\n"), std::nullopt);
  EXPECT_EQ(BytesFromMemoryMax(""), std::nullopt);
}

// Requirements: NFR-19
TEST(DescribeMachine, WritesEachField) {
  const Machine machine{.os = "Windows 10.0.26100 (24H2)",
                        .cpu = {.model = "AMD Ryzen 9 7950X", .logical_cores = 32},
                        .memory = {.total_bytes = 64ULL << 30, .available_bytes = 40ULL << 30},
                        .limits = {.cpus = 2.5, .memory_bytes = 4ULL << 30}};
  EXPECT_EQ(DescribeMachine(machine),
            "os=\"Windows 10.0.26100 (24H2)\" cpu=\"AMD Ryzen 9 7950X\" logical_cores=32 memory_mib=65536 "
            "memory_available_mib=40960 cpu_limit=2.5 memory_limit_mib=4096");
}

// Requirements: NFR-19
TEST(DescribeMachine, SaysUnknownForWhatTheOsDidNotTell) {
  EXPECT_EQ(
      DescribeMachine(Machine{}),
      "os=unknown cpu=unknown logical_cores=unknown memory_mib=unknown memory_available_mib=unknown cpu_limit=none "
      "memory_limit_mib=none");
}

// The allocator a bug ran on (ADR-0078): mimalloc, with its version as
// mi_version() gives it.
// Requirements: NFR-19
TEST(DescribeAllocator, NamesMimallocAndItsVersion) {
  EXPECT_EQ(DescribeAllocator(353), "allocator=mimalloc/3.5.3");
  EXPECT_EQ(DescribeAllocator(217), "allocator=mimalloc/2.1.7");
}

// A sanitizer build reads no mimalloc version: its sanitizer's allocator serves.
// Requirements: NFR-19
TEST(DescribeAllocator, NamesThePlatformsWithoutMimalloc) {
  EXPECT_EQ(DescribeAllocator(std::nullopt), "allocator=platform");
}

// The machine running the test reports the fields every supported OS has.
// Requirements: NFR-19
TEST(QueryMachine, FindsTheOsCoresAndMemory) {
  const Machine machine = QueryMachine();
  EXPECT_FALSE(machine.os.empty());
  EXPECT_GT(machine.cpu.logical_cores, 0U);
  EXPECT_GT(machine.memory.total_bytes, 0U);
}

// Requirements: NFR-19
TEST(GpuFromUevent, ReadsThePciIdsAndTheDriver) {
  const std::optional<Gpu> gpu = GpuFromUevent(
      "DRIVER=nvidia\n"
      "PCI_CLASS=30000\n"
      "PCI_ID=10DE:2684\n"
      "PCI_SUBSYS_ID=10DE:167C\n"
      "PCI_SLOT_NAME=0000:01:00.0\n");
  ASSERT_TRUE(gpu.has_value());
  EXPECT_EQ(gpu->vendor_id, 0x10deU);
  EXPECT_EQ(gpu->device_id, 0x2684U);
  EXPECT_EQ(gpu->driver, "nvidia");
  EXPECT_EQ(gpu->name, "");
  EXPECT_EQ(gpu->driver_version, "");
}

// A virtual card, or text that is not a uevent, names no GPU.
// Requirements: NFR-19
TEST(GpuFromUevent, IsNulloptWithoutAPciId) {
  EXPECT_FALSE(GpuFromUevent("DRIVER=vkms\n").has_value());
  EXPECT_FALSE(GpuFromUevent("PCI_ID=10DE\n").has_value());
  EXPECT_FALSE(GpuFromUevent("PCI_ID=XYZ:2684\n").has_value());
}

// Requirements: NFR-19
TEST(DescribeGpu, WritesEachFieldNamingAKnownVendor) {
  EXPECT_EQ(DescribeGpu(Gpu{.name = "NVIDIA GeForce RTX 4090",
                            .vendor_id = 0x10de,
                            .device_id = 0x2684,
                            .driver = "nvidia",
                            .driver_version = "560.35.03"}),
            "name=\"NVIDIA GeForce RTX 4090\" vendor=NVIDIA device_id=0x2684 driver=\"nvidia\" "
            "driver_version=\"560.35.03\"");
}

// Requirements: NFR-19
TEST(DescribeGpu, SaysUnknownForWhatTheOsDidNotTellAndAnUnknownVendorsId) {
  EXPECT_EQ(DescribeGpu(Gpu{.name = {}, .vendor_id = 0x1af4, .device_id = 0x1050, .driver = {}, .driver_version = {}}),
            "name=unknown vendor=0x1af4 device_id=0x1050 driver=unknown driver_version=unknown");
}

}  // namespace
}  // namespace augusta::hardware

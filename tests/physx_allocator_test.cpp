#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "physx_state.h"

// The allocator PhysX's foundation is created with: every block it hands out is
// 16-byte aligned, whatever malloc the process runs (#1401).
namespace {

using augusta::physics::PhysxAllocator;

constexpr std::uintptr_t kPhysxAlignment = 16;

// Requirements: NFR-04
TEST(PhysxAllocatorTest, EverySizeComesBack16ByteAligned) {
  PhysxAllocator allocator;
  std::vector<std::size_t> sizes;
  for (std::size_t size = 1; size <= 16; ++size) {
    sizes.push_back(size);
  }
  sizes.insert(sizes.end(), {17, 24, 31, 33, 48, 100, 1000, 4096, 65537});

  std::vector<void*> blocks;
  for (const std::size_t size : sizes) {
    void* block = allocator.allocate(size, "test", __FILE__, __LINE__);
    EXPECT_NE(block, nullptr) << "size " << size;
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(block) % kPhysxAlignment, 0U) << "size " << size;
    blocks.push_back(block);
  }
  for (void* block : blocks) {
    allocator.deallocate(block);
  }
  // Null is accepted, as free() accepts it.
  allocator.deallocate(nullptr);
}

}  // namespace

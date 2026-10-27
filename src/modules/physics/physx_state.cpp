#include "physx_state.h"

#include <cstddef>
#include <mutex>
#include <new>
#include <stdexcept>

// The headers this file uses, not PxPhysicsAPI.h: that one also pulls in
// PxVecMath.h, whose Windows SIMD path reads cl's __m128 union members
// (m128_u16), which clang-cl's __m128 doesn't have.
#include <PxPhysics.h>
#include <common/PxTolerancesScale.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxFoundation.h>
#include <foundation/PxPhysicsVersion.h>
#if AUGUSTA_DEBUG_TOOLS
#include <omnipvd/PxOmniPvd.h>
#endif

#include "augusta/logging.h"

namespace augusta::physics {

namespace {

// The alignment PhysX requires of every block its allocator returns.
constexpr std::align_val_t kPhysxAlignment{16};

PhysxHandles AcquirePhysx() {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.users == 0) {
    CreateFoundation(state);
    CreatePhysics(state);
  }
  ++state.users;
  return {.foundation = state.foundation, .physics = state.physics};
}

void ReleasePhysx() {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (--state.users == 0) {
    ReleaseProcessState(state);
  }
}

}  // namespace

void LogErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) {
  LOG_ERROR("subsystem=physics event=physx_error code={} message=\"{}\" file={} line={}", static_cast<int>(code),
            message, file, line);
}

// Null on failure, as PxDefaultAllocator's malloc gave, rather than a throw
// through PhysX's own code.
void* PhysxAllocator::allocate(std::size_t size, const char* /*type_name*/, const char* /*file*/, int /*line*/) {
  return ::operator new(size, kPhysxAlignment, std::nothrow);
}

void PhysxAllocator::deallocate(void* ptr) { ::operator delete(ptr, kPhysxAlignment); }

PhysxProcessState& GlobalState() {
  static PhysxProcessState state;
  return state;
}

void CreateFoundation(PhysxProcessState& state) {
  state.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, state.allocator, state.error_callback);
  if (state.foundation == nullptr) {
    throw std::runtime_error("physics::World: PxCreateFoundation failed");
  }
}

void ReleaseProcessState(PhysxProcessState& state) {
  if (state.physics != nullptr) {
    state.physics->release();
    state.physics = nullptr;
  }
#if AUGUSTA_DEBUG_TOOLS
  if (state.omni_pvd != nullptr) {
    state.omni_pvd->release();
    state.omni_pvd = nullptr;
  }
#endif
  if (state.foundation != nullptr) {
    state.foundation->release();
    state.foundation = nullptr;
  }
}

void CreatePhysics(PhysxProcessState& state) {
  const physx::PxTolerancesScale scale;
#if AUGUSTA_DEBUG_TOOLS
  state.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *state.foundation, scale, true, nullptr, state.omni_pvd);
#else
  state.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *state.foundation, scale, true, nullptr);
#endif
  if (state.physics == nullptr) {
    ReleaseProcessState(state);
    throw std::runtime_error("physics::World: PxCreatePhysics failed");
  }
}

PhysxLease::PhysxLease() : handles_(AcquirePhysx()) {}

PhysxLease::~PhysxLease() { ReleasePhysx(); }

}  // namespace augusta::physics

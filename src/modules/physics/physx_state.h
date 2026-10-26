#ifndef AUGUSTA_PHYSICS_PHYSX_STATE_H_
#define AUGUSTA_PHYSICS_PHYSX_STATE_H_

#include <mutex>

// The headers this file uses, not PxPhysicsAPI.h: that one also pulls in
// PxVecMath.h, whose Windows SIMD path reads cl's __m128 union members
// (m128_u16), which clang-cl's __m128 doesn't have.
#include <PxPhysics.h>
#include <extensions/PxDefaultAllocator.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxFoundation.h>
#if AUGUSTA_DEBUG_TOOLS
#include <omnipvd/PxOmniPvd.h>
#endif

/// \file
/// The process's one PhysX foundation and physics, private to the module: every
/// World holds a use of them (PhysxLease) for as long as it lives, and a
/// Recording (Development build) makes them recorded before the first World
/// does (ADR-0074).
namespace augusta::physics {

// Routes PhysX's own diagnostics through this engine's logging (ADR-0029)
// instead of PhysX's default stderr output.
class LogErrorCallback : public physx::PxErrorCallback {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming) - name mandated by PxErrorCallback's own virtual signature.
  void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override;
};

// PhysX allows one PxFoundation per process, and creating PxPhysics on it is
// costly, so every World shares one of each, created by the first World and
// released with the last - a server and several clients in one test process
// each own a World.
struct PhysxProcessState {
  std::mutex mutex;
  int users = 0;
  // Must outlive the foundation, so they live here rather than in each World.
  LogErrorCallback error_callback;
  physx::PxDefaultAllocator allocator;
  physx::PxFoundation* foundation = nullptr;
#if AUGUSTA_DEBUG_TOOLS
  // What samples physics into a recording (StartRecording); null when none does.
  physx::PxOmniPvd* omni_pvd = nullptr;
#endif
  physx::PxPhysics* physics = nullptr;
};

PhysxProcessState& GlobalState();

struct PhysxHandles {
  physx::PxFoundation* foundation;
  physx::PxPhysics* physics;
};

void CreateFoundation(PhysxProcessState& state);

// Releases what the state holds, in the reverse of the order it was made in:
// physics before what samples it, and the foundation last.
void ReleaseProcessState(PhysxProcessState& state);

// Creates physics on the state's foundation, sampled by its omni_pvd if it has one.
void CreatePhysics(PhysxProcessState& state);

// Holds one use of the shared PhysX state for as long as it lives. Being a
// member, it is released even if the owning World's constructor throws after
// it was acquired, when the World's destructor would not run.
class PhysxLease {
 public:
  PhysxLease();
  // Takes over a use already counted (StartRecording's).
  explicit PhysxLease(PhysxHandles counted) : handles_(counted) {}
  ~PhysxLease();
  PhysxLease(const PhysxLease&) = delete;
  PhysxLease& operator=(const PhysxLease&) = delete;

  [[nodiscard]] physx::PxPhysics* Physics() const { return handles_.physics; }

 private:
  PhysxHandles handles_;
};

}  // namespace augusta::physics

#endif  // AUGUSTA_PHYSICS_PHYSX_STATE_H_

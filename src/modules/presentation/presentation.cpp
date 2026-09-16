#include "augusta/presentation.h"

#include <flecs.h>

#include <array>

#include "augusta/animation.h"
#include "augusta/logging.h"

namespace augusta::presentation {

namespace {

constexpr std::size_t kPhaseCount = 5;
using PhaseEntities = std::array<flecs::entity, kPhaseCount>;

enum PhaseIndex : std::size_t {
  kInterpolation = 0,
  kCamera,
  kAnimation,
  kAudioCues,
  kCommit,
};

}  // namespace

struct World::Impl {
  flecs::world ecs;
  audio::Engine& audio_engine;
  animation::Engine animation;
  PhaseEntities phases;
  // The previous call's latest, for kInterpolation to blend against (see
  // RunFrame's doc comment in presentation.h). Unset until the first
  // RunFrame call completes.
  prediction::State previous_state;
  bool has_previous_state = false;

  explicit Impl(audio::Engine& engine) : audio_engine(engine) {
    // Chain the five phases in Phase's declared order (ADR-0024): each
    // depends_on the previous one, and the first depends on Flecs's
    // built-in OnUpdate phase, so a single ecs.progress() call runs them
    // in exactly this sequence.
    phases[kInterpolation] = ecs.entity("Interpolation").add(flecs::Phase).depends_on(flecs::OnUpdate);
    phases[kCamera] = ecs.entity("Camera").add(flecs::Phase).depends_on(phases[kInterpolation]);
    phases[kAnimation] = ecs.entity("Animation").add(flecs::Phase).depends_on(phases[kCamera]);
    phases[kAudioCues] = ecs.entity("AudioCues").add(flecs::Phase).depends_on(phases[kAnimation]);
    phases[kCommit] = ecs.entity("Commit").add(flecs::Phase).depends_on(phases[kAudioCues]);

    // One system per phase, matching the responsibility documented on
    // Phase's matching enumerator in presentation.h. Each uses run()
    // rather than each(): it fires exactly once per RunFrame regardless
    // of matched entities, since ECS component shapes aren't designed
    // yet (see presentation.h's header comment). Bodies are stubs until
    // those shapes exist, and until RunFrame's per-call latest argument
    // has somewhere to flow into the ECS (a singleton, presumably, once
    // one is designed).
    ecs.system("InterpolationSystem").kind(phases[kInterpolation]).run([](flecs::iter&) {
      LT("subsystem=presentationworld event=interpolation");
      // TODO(sergioffpc): blend the last two prediction::State values.
    });
    ecs.system("CameraSystem").kind(phases[kCamera]).run([](flecs::iter&) {
      LT("subsystem=presentationworld event=camera");
      // TODO(sergioffpc): not yet a module of its own - see presentation.h.
    });
    ecs.system("AnimationSystem").kind(phases[kAnimation]).run([this](flecs::iter&) {
      LT("subsystem=presentationworld event=animation");
      // TODO(sergioffpc): animation.Update per visible player character,
      // once there's a per-character handle to iterate and a
      // animation::LocomotionInput to build from interpolated movement -
      // see presentation.h's Phase::kAnimation doc comment. The capture
      // only proves animation is reachable from here; no call is made
      // yet.
      (void)animation;
    });
    ecs.system("AudioCuesSystem").kind(phases[kAudioCues]).run([this](flecs::iter&) {
      LT("subsystem=presentationworld event=audio_cues");
      // TODO(sergioffpc): audio_engine.SetListener then PlaySound per
      // this frame's cues - see presentation.h's Phase::kAudioCues doc
      // comment. The capture only proves audio_engine is reachable from
      // here; no call is made yet.
      (void)audio_engine;
    });
    ecs.system("CommitSystem").kind(phases[kCommit]).run([](flecs::iter&) {
      LT("subsystem=presentationworld event=commit");
      // TODO(sergioffpc): package the frame's presentation data into State.
    });
  }
};

World::World(audio::Engine& audio_engine) : impl_(std::make_unique<Impl>(audio_engine)) {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

State World::RunFrame(const prediction::State& latest) {
  impl_->ecs.progress();
  impl_->previous_state = latest;
  impl_->has_previous_state = true;
  return State{};
}

}  // namespace augusta::presentation

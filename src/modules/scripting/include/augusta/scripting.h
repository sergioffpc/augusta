#ifndef AUGUSTA_SCRIPTING_H_
#define AUGUSTA_SCRIPTING_H_

#include <string>

// augusta::scripting embeds Lua (ADR-0022, via sol2) to run game policy -
// round lifecycle, win conditions, spawn rules - as sandboxed scripts
// (no io, os.execute, package.loadlib, or other filesystem/network
// access, per §8), upholding "no I/O inside ECS worlds" structurally
// rather than by convention alone. Server-only: game policy is
// exclusively server-authoritative, never run by either client world.
//
// SimulationWorld's Scripts/Behaviours phase (ADR-0023) runs last in the
// tick, after C++ mechanism phases (Movement, Ballistics, HitDetection,
// Damage) have already resolved this tick's physical facts - so a hook
// running now can react to e.g. deaths that just happened and schedule
// what follows (spawns, round transitions) for the next tick. Unlike
// Renderer/Input/Networking, there's no cross-thread story here: the
// Scripts/Behaviours phase runs entirely within SimulationWorld on the
// Simulation thread (ADR-0005), same as every other phase.
//
// RunHook takes a plain string, not a fixed C++ enum the way e.g.
// window::Key or networking::PeerEventType do: the entire point of
// policy living in Lua (ADR-0022) is that new hooks and behavior don't
// need a C++ recompile, so the hook catalogue is a convention shared
// between SimulationWorld's orchestrator and the loaded script, not a
// type this header enforces.
//
// What a hook function can actually read or do once invoked - the
// curated game-state queries and actions exposed into the sandbox (e.g.
// "which players just died," "assign this player's spawn point," "end
// the round") isn't designed yet: it depends on ECS component shapes
// (Flecs, ADR-0001) and Match/Round State, neither of which exist as
// augusta types yet. Revisit this header once those do - same kind of
// deliberate TBD as Renderer's "what gets drawn" or Networking's
// message catalogue.
namespace augusta::scripting {

// Owns one sandboxed Lua environment hosting a match's game-policy
// script. SimulationWorld constructs exactly one.
class Engine {
 public:
  // Starts with no script loaded, so every RunHook is a no-op until
  // game policy (ROADMAP.md M5) gives it the match's script from the
  // server pack.
  Engine() = default;

  // Invokes the Lua function registered for hook_name, if the script
  // defines one - a no-op otherwise (a script that doesn't react to a
  // given hook simply doesn't define it). Call from SimulationWorld's
  // Scripts/Behaviours phase, once per tick per relevant hook.
  void RunHook(const std::string& hook_name);
};

}  // namespace augusta::scripting

#endif  // AUGUSTA_SCRIPTING_H_

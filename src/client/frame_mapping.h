#ifndef AUGUSTA_CLIENT_FRAME_MAPPING_H_
#define AUGUSTA_CLIENT_FRAME_MAPPING_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"

/// \file
/// The conversions ClientRuntime makes at its edges, each frame and each tick:
/// what the harness received into presentation's own types, so presentation does
/// not depend on the network session (ADR-0038), and what presentation decided
/// into what the renderer draws, so neither depends on the other.
namespace augusta::client {

/// Maps one interpolated remote player into a renderer-drawable instance of its
/// character's mesh, which ClientRuntime uploads via SetCharacterMesh in the Lobby
/// (ADR-0042/ADR-0043), turned where it faces. The mesh is drawn as authored,
/// standing: its stance shows once animation poses it.
[[nodiscard]] renderer::RemotePlayer ToRenderer(const presentation::RemotePlayer& remote);

/// Maps this frame's presentation::Camera into what Renderer::SetCamera takes.
[[nodiscard]] renderer::Camera ToRenderer(const presentation::Camera& camera);

/// Maps effects that show for lifetime seconds into glows, faded by their age.
[[nodiscard]] std::vector<renderer::Glow> ToRenderer(const std::vector<presentation::Effect>& effects, float lifetime);

/// This frame's tracers, impacts and muzzle flashes, as Renderer::SetCombatEffects takes them.
[[nodiscard]] renderer::CombatEffects CombatEffectsOf(const presentation::State& state);

/// Maps the harness's Entity ID into presentation's own - the same number.
[[nodiscard]] presentation::EntityId ToPresentation(harness::EntityId entity);

/// An Authoritative State update as presentation's WorldSnapshot: the same tick,
/// the duration of a tick at the server's tick_rate_hz, and every body.
[[nodiscard]] presentation::WorldSnapshot ToPresentation(const harness::AuthoritativeState& state,
                                                         std::uint8_t tick_rate_hz);

/// Maps a Shot as the harness received it into presentation's own, which draws
/// it: who fired it and where from and for; its tick is the server's business.
[[nodiscard]] presentation::Shot ToPresentation(const harness::Shot& shot);

[[nodiscard]] presentation::Aim ToPresentation(const input::Aim& aim);

/// view's newest Authoritative State update as presentation's WorldSnapshot, or
/// nullopt outside a match. An Authoritative State only follows Join accepted,
/// which told the tick rate.
[[nodiscard]] std::optional<presentation::WorldSnapshot> SnapshotOf(const harness::ServerView& view);

/// Every player's character as Match start named it, in Session order, for
/// PresentationWorld::RunFrame; empty before the first match. Only the
/// characters: presentation needs nothing else of Match start.
[[nodiscard]] std::vector<presentation::PlayerCharacter> CharactersOf(
    const std::optional<harness::MatchStart>& match_start);

/// What view says the match this client was last in ended with, its winner named
/// by the body it played, from that match's Match start; nullopt before the first
/// ends and while one is in progress. A winner missing from Match start is none.
[[nodiscard]] std::optional<presentation::MatchEnd> MatchEndOf(const harness::ServerView& view);

/// command as sampled against seen_time, the Seen time of the last render frame:
/// what it reports to the server, which judges its shots against the players as
/// they were then (ADR-0044). With no Seen time, it reports none.
[[nodiscard]] command::Command WithSeenTime(command::Command command,
                                            const std::optional<presentation::SeenTime>& seen_time);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_FRAME_MAPPING_H_

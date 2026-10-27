#include <cstddef>
#include <cstdint>
#include <exception>  // IWYU pragma: keep - rapidcheck/Gen.hpp uses it without including it
#include <ostream>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/grid.h"
#include "augusta/limits.h"
#include "augusta/math.h"
#include "augusta/protocol.h"

// Property-based tests of a Development build's messages (ADR-0038, ADR-0074),
// as protocol_property_test.cpp has the rest's.
namespace augusta::protocol {

// How RapidCheck prints a failing case, found by argument-dependent lookup.
void showValue(const DebugSubscribeWire& subscribe, std::ostream& out) {
  out << "DebugSubscribe{flags " << +subscribe.flags << "}";
}

void showValue(const ShotJudgementWire& judgement, std::ostream& out) {
  out << "ShotJudgement{shot_tick " << judgement.shot_tick << ", delay " << +judgement.delay_ticks << " + "
      << judgement.delay_fraction << ", struck " << +static_cast<std::uint8_t>(judgement.struck) << ", part "
      << +static_cast<std::uint8_t>(judgement.part) << ", flags " << +judgement.flags << ", point ("
      << judgement.point.x << ", " << judgement.point.y << ", " << judgement.point.z << "), target "
      << static_cast<std::uint32_t>(judgement.target) << ", entries [";
  for (const ShotJudgementEntryWire& entry : judgement.entries) {
    out << "{" << entry.ticks_after_shot << ":";
    for (const JudgedPlayerWire& player : entry.players) {
      out << " " << static_cast<std::uint32_t>(player.entity) << "@(" << player.position.x << ", " << player.position.y
          << ", " << player.position.z << ") yaw " << player.yaw << " stance "
          << +static_cast<std::uint8_t>(player.stance);
    }
    out << "}";
  }
  out << "]}";
}

}  // namespace augusta::protocol

namespace {

using augusta::limits::kMaxShotJudgementEntries;
using augusta::math::FromSteps;
using augusta::math::Grid;
using augusta::math::kAngleGrid;
using augusta::math::kFractionGrid;
using augusta::math::kPositionGrid;
using augusta::math::Vec3;
using augusta::protocol::BodyPartWire;
using augusta::protocol::DebugSubscribeWire;
using augusta::protocol::Decode;
using augusta::protocol::Encode;
using augusta::protocol::EntityIdWire;
using augusta::protocol::JudgedPlayerWire;
using augusta::protocol::MessageWire;
using augusta::protocol::ShotJudgementEntryWire;
using augusta::protocol::ShotJudgementWire;
using augusta::protocol::StanceWire;
using augusta::protocol::StruckWire;

// A value on grid, anywhere in its range: the only numbers the codec must
// carry exactly.
rc::Gen<float> OnGrid(const Grid& grid) {
  return rc::gen::map(rc::gen::inRange<std::int64_t>(grid.min, std::int64_t{grid.max} + 1),
                      [grid](std::int64_t steps) { return FromSteps(static_cast<std::int32_t>(steps), grid); });
}

rc::Gen<Vec3> Vec3OnGrid(const Grid& grid) {
  return rc::gen::apply([](float x, float y, float z) { return Vec3(x, y, z); }, OnGrid(grid), OnGrid(grid),
                        OnGrid(grid));
}

rc::Gen<EntityIdWire> AnyEntity() {
  return rc::gen::map(rc::gen::arbitrary<std::uint32_t>(),
                      [](std::uint32_t id) { return static_cast<EntityIdWire>(id); });
}

// A list of at most max_size elements.
template <typename T>
rc::Gen<std::vector<T>> UpTo(std::size_t max_size, rc::Gen<T> element) {
  return rc::gen::mapcat(rc::gen::inRange<std::size_t>(0, max_size + 1),
                         [element](std::size_t size) { return rc::gen::container<std::vector<T>>(size, element); });
}

rc::Gen<JudgedPlayerWire> JudgedPlayer() {
  return rc::gen::build<JudgedPlayerWire>(
      rc::gen::set(&JudgedPlayerWire::position, Vec3OnGrid(kPositionGrid)),
      rc::gen::set(&JudgedPlayerWire::entity, AnyEntity()), rc::gen::set(&JudgedPlayerWire::yaw, OnGrid(kAngleGrid)),
      rc::gen::set(&JudgedPlayerWire::stance,
                   rc::gen::element(StanceWire::kStanding, StanceWire::kCrouching, StanceWire::kProne)));
}

rc::Gen<ShotJudgementEntryWire> Entry() {
  return rc::gen::build<ShotJudgementEntryWire>(
      rc::gen::set(&ShotJudgementEntryWire::players, UpTo(augusta::limits::kMaxPlayers, JudgedPlayer())),
      rc::gen::set(&ShotJudgementEntryWire::ticks_after_shot, rc::gen::arbitrary<std::uint16_t>()));
}

rc::Gen<ShotJudgementWire> ShotJudgement() {
  return rc::gen::build<ShotJudgementWire>(
      rc::gen::set(&ShotJudgementWire::shot_tick, rc::gen::arbitrary<std::uint64_t>()),
      rc::gen::set(&ShotJudgementWire::entries, UpTo(kMaxShotJudgementEntries, Entry())),
      rc::gen::set(&ShotJudgementWire::point, Vec3OnGrid(kPositionGrid)),
      rc::gen::set(&ShotJudgementWire::target, AnyEntity()),
      rc::gen::set(&ShotJudgementWire::delay_fraction, OnGrid(kFractionGrid)),
      rc::gen::set(&ShotJudgementWire::delay_ticks, rc::gen::arbitrary<std::uint8_t>()),
      rc::gen::set(&ShotJudgementWire::struck,
                   rc::gen::element(StruckWire::kNothing, StruckWire::kMap, StruckWire::kProp, StruckWire::kHitbox)),
      rc::gen::set(&ShotJudgementWire::part,
                   rc::gen::element(BodyPartWire::kHead, BodyPartWire::kTorso, BodyPartWire::kLimb)),
      rc::gen::set(&ShotJudgementWire::flags, rc::gen::element(std::uint8_t{0}, ShotJudgementWire::kEntriesLeftOut)));
}

// Every byte of flags is one a client may send, a bit no stream uses included.
// Requirements: US-30, NFR-20
RC_GTEST_PROP(ProtocolPropertyDebugTest, EveryDebugSubscribeSurvivesEncodeThenDecode, ()) {
  const DebugSubscribeWire subscribe{.flags = *rc::gen::arbitrary<std::uint8_t>()};

  const auto decoded = Decode(Encode(subscribe).value());

  RC_ASSERT(decoded.has_value());
  RC_ASSERT(*decoded == MessageWire{subscribe});
}

// Every number on its grid and within its range, 0 to 16 entries of 0 to every
// player a Match holds each, the left-out flag set or not.
// Requirements: US-30, NFR-20
RC_GTEST_PROP(ProtocolPropertyDebugTest, EveryShotJudgementSurvivesEncodeThenDecode, ()) {
  const ShotJudgementWire judgement = *ShotJudgement();

  const auto decoded = Decode(Encode(judgement).value());

  RC_ASSERT(decoded.has_value());
  RC_ASSERT(*decoded == MessageWire{judgement});
}

}  // namespace

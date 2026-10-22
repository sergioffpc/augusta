#include <cstdint>
#include <exception>  // IWYU pragma: keep - rapidcheck/Gen.hpp uses it without including it
#include <ostream>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "augusta/protocol.h"

// Property-based tests of a Development build's messages (ADR-0038, ADR-0074),
// as protocol_property_test.cpp has the rest's.
namespace augusta::protocol {

// How RapidCheck prints a failing case, found by argument-dependent lookup.
void showValue(const DebugSubscribeWire& subscribe, std::ostream& out) {
  out << "DebugSubscribe{flags " << +subscribe.flags << "}";
}

}  // namespace augusta::protocol

namespace {

using augusta::protocol::DebugSubscribeWire;
using augusta::protocol::Decode;
using augusta::protocol::Encode;
using augusta::protocol::MessageWire;

// Every byte of flags is one a client may send, a bit no stream uses included.
RC_GTEST_PROP(ProtocolPropertyDebugTest, EveryDebugSubscribeSurvivesEncodeThenDecode, ()) {
  const DebugSubscribeWire subscribe{.flags = *rc::gen::arbitrary<std::uint8_t>()};

  const auto decoded = Decode(Encode(subscribe).value());

  RC_ASSERT(decoded.has_value());
  RC_ASSERT(*decoded == MessageWire{subscribe});
}

}  // namespace

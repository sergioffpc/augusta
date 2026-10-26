#ifndef AUGUSTA_PROTOCOL_BITFIELDS_H_
#define AUGUSTA_PROTOCOL_BITFIELDS_H_

#include <cstdint>

#include "augusta/protocol.h"

/// \file
/// The layout of the bitfields a message packs its flags and enumerations into,
/// which the encoder writes and the decoder checks. Private to the module
/// (ADR-0038).
namespace augusta::protocol {

/// A command's flags take bits 0-3 of its bitfield and its stance bits 4-5
/// (ADR-0038).
constexpr std::uint8_t kCommandFlagsMask = 0x0FU;
constexpr unsigned kCommandStanceShift = 4U;
constexpr std::uint32_t kStanceMask = 0x03U;
constexpr std::uint32_t kCommandBitsMask = kCommandFlagsMask | (kStanceMask << kCommandStanceShift);

/// A body's stance takes bits 0-1 of its bitfield, its flags the bits from 2 up
/// (exhausted bit 2, ADS bit 3, grounded bit 4), and its previous stance bits 5-6
/// (ADR-0038).
constexpr std::uint8_t kBodyFlagsMask = BodyStateWire::kExhausted | BodyStateWire::kAds | BodyStateWire::kGrounded;
constexpr unsigned kBodyFlagsShift = 2U;
constexpr unsigned kPreviousStanceShift = 5U;
constexpr std::uint32_t kBodyBitsMask =
    kStanceMask | (std::uint32_t{kBodyFlagsMask} << kBodyFlagsShift) | (kStanceMask << kPreviousStanceShift);

#if AUGUSTA_DEBUG_TOOLS
/// A Shot judgement's bitfield (ADR-0038): what the round struck in bits 0-1,
/// the Body part in bits 2-3, its flags from bit 4.
constexpr std::uint32_t kStruckMask = 0x03U;
constexpr unsigned kJudgementPartShift = 2U;
constexpr std::uint32_t kPartMask = 0x03U;
constexpr unsigned kJudgementFlagsShift = 4U;
constexpr std::uint8_t kJudgementFlagsMask = ShotJudgementWire::kEntriesLeftOut;
constexpr std::uint32_t kJudgementBitsMask =
    kStruckMask | (kPartMask << kJudgementPartShift) | (std::uint32_t{kJudgementFlagsMask} << kJudgementFlagsShift);
#endif

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_BITFIELDS_H_

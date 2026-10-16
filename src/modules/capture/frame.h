#ifndef AUGUSTA_CAPTURE_FRAME_H_
#define AUGUSTA_CAPTURE_FRAME_H_

#include <cstddef>
#include <expected>
#include <vector>

#include "augusta/capture.h"
#include "augusta/protocol.h"

/// \file
/// How the capture module frames a record it encoded, internal to it and apart
/// so a test reaches the frame's limit, which no record the codec accepts
/// passes (#463).
namespace augusta::capture {

/// How many bytes a frame's length takes, before its payload.
inline constexpr std::size_t kFrameLengthSize = 1;

/// The longest payload a frame holds, in bytes: none of a capture's records,
/// header included, passes it, so kFrameLengthSize of length is enough (#463).
inline constexpr std::size_t kMaxFramePayload = 255;

/// The frame of payload, the codec's encoding of a record of type: its length
/// in one byte, then it. A codec error is the writer's broken invariant
/// (failure::Code::kInvariantViolated, named by type); a payload longer than
/// kMaxFramePayload is RecordTooLong.
[[nodiscard]] std::expected<std::vector<std::byte>, EncodeError> Framed(
    const std::expected<protocol::BytesWire, protocol::EncodeError>& payload, protocol::CaptureRecordTypeWire type);

}  // namespace augusta::capture

#endif  // AUGUSTA_CAPTURE_FRAME_H_

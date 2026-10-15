#ifndef AUGUSTA_CAPTURE_FRAME_H_
#define AUGUSTA_CAPTURE_FRAME_H_

#include <cstddef>
#include <expected>
#include <vector>

#include "augusta/capture.h"
#include "augusta/protocol.h"

/// \file
/// How the capture module frames a record it encoded, apart so a test reaches
/// the frame's limit, which no record the codec accepts passes (#463).
namespace augusta::capture {

/// The frame of payload, the codec's encoding of a record of type: its length
/// in one byte, then it. A codec error is the writer's broken invariant
/// (failure::Code::kInvariantViolated, named by type); a payload longer than
/// kMaxFramePayload is RecordTooLong.
[[nodiscard]] std::expected<std::vector<std::byte>, EncodeError> Framed(
    const std::expected<protocol::BytesWire, protocol::EncodeError>& payload, protocol::CaptureRecordTypeWire type);

}  // namespace augusta::capture

#endif  // AUGUSTA_CAPTURE_FRAME_H_

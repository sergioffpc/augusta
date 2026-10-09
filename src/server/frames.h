#ifndef AUGUSTA_SERVER_FRAMES_H_
#define AUGUSTA_SERVER_FRAMES_H_

#include <cstddef>

#include "augusta/capture_file.h"

/// \file
/// How a Match recording (recording.h, ADR-0048) holds its records one after
/// another: framed as a Match capture is (augusta/capture_file.h), each
/// record's payload after its length, but that length in 4 bytes, since its
/// records hold whole Authoritative States.
namespace augusta::server {

using capture_file::Frame;
using capture_file::FrameFormat;
using capture_file::ReadFrame;
using capture_file::WriteFrame;

/// A Match recording's frames: its records hold whole Authoritative States.
inline constexpr FrameFormat kRecordingFrames{.length_bytes = 4, .max_payload = std::size_t{64} * 1024};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_FRAMES_H_

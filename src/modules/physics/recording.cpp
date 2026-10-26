#include "augusta/physics.h"

#if AUGUSTA_DEBUG_TOOLS
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// The headers this file uses, not PxPhysicsAPI.h: that one also pulls in
// PxVecMath.h, whose Windows SIMD path reads cl's __m128 union members
// (m128_u16), which clang-cl's __m128 doesn't have.
#include <omnipvd/PxOmniPvd.h>
#include <pvdruntime/OmniPvdFileWriteStream.h>
#include <pvdruntime/OmniPvdWriter.h>

#include "augusta/logging.h"
#include "physx_state.h"
#endif

namespace augusta::physics {

#if AUGUSTA_DEBUG_TOOLS
namespace {

// Why PxCreateOmniPvd made nothing. Only a Debug PhysX has OmniPVD, every other
// build compiling it out, and the PhysX package hands a Debug build _DEBUG.
#ifdef _DEBUG
constexpr std::string_view kNoOmniPvd = "OmniPVD's runtime (PVDRuntime_64) did not load from beside the executable";
#else
constexpr std::string_view kNoOmniPvd = "this build's PhysX has no OmniPVD: only a Debug build records";
#endif

// Points the state's new omni_pvd at a file stream writing file, or says why it cannot.
std::expected<void, RecordingError> OpenRecording(PhysxProcessState& state, const std::filesystem::path& file) {
  state.omni_pvd = PxCreateOmniPvd(*state.foundation);
  if (state.omni_pvd == nullptr) {
    return std::unexpected(RecordingError::kUnavailable);
  }
  OmniPvdWriter* writer = state.omni_pvd->getWriter();
  OmniPvdFileWriteStream* stream = state.omni_pvd->getFileWriteStream();
  if (writer == nullptr || stream == nullptr) {
    return std::unexpected(RecordingError::kRefused);
  }
  stream->setFileName(file.string().c_str());
  writer->setWriteStream(*stream);
  return {};
}

// Makes the shared PhysX state recorded into file and counts the recording's
// use of it, or says why it cannot, leaving the state as it was.
std::expected<PhysxHandles, RecordingError> AcquireRecordedPhysx(const std::filesystem::path& file) {
  PhysxProcessState& state = GlobalState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.users != 0) {
    return std::unexpected(RecordingError::kWorldExists);
  }
  CreateFoundation(state);
  if (auto opened = OpenRecording(state, file); !opened) {
    ReleaseProcessState(state);
    return std::unexpected(opened.error());
  }
  CreatePhysics(state);
  if (!state.omni_pvd->startSampling()) {
    ReleaseProcessState(state);
    return std::unexpected(RecordingError::kRefused);
  }
  ++state.users;
  return PhysxHandles{.foundation = state.foundation, .physics = state.physics};
}

}  // namespace

std::string_view DescribeRecordingError(RecordingError error) {
  switch (error) {
    case RecordingError::kWorldExists:
      return "a World already exists, and PhysX was made without the recording";
    case RecordingError::kUnavailable:
      return kNoOmniPvd;
    case RecordingError::kRefused:
      return "OmniPVD gave no writer or file stream, or would not start sampling";
  }
  return "unknown recording error";
}

struct Recording::Impl {
  Impl(PhysxHandles counted, std::filesystem::path recorded) : lease(counted), file(std::move(recorded)) {}

  PhysxLease lease;
  std::filesystem::path file;
};

Recording::Recording(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Recording::~Recording() = default;
Recording::Recording(Recording&&) noexcept = default;
Recording& Recording::operator=(Recording&&) noexcept = default;

const std::filesystem::path& Recording::File() const { return impl_->file; }

std::expected<Recording, RecordingError> StartRecording(std::string_view executable) {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  std::filesystem::path file =
      std::filesystem::temp_directory_path() / std::format("{}-{:%Y%m%dT%H%M%SZ}.ovd", executable, now);
  const auto handles = AcquireRecordedPhysx(file);
  if (!handles) {
    return std::unexpected(handles.error());
  }
  LOG_INFO("subsystem=physics event=recording_started path=\"{}\"", file.string());
  return Recording(std::make_unique<Recording::Impl>(*handles, std::move(file)));
}

std::optional<Recording> StartRecordingOrWarn(std::string_view executable, std::string_view key) {
  std::expected<Recording, RecordingError> started = StartRecording(executable);
  if (!started) {
    LOG_WARN("subsystem=physics event=recording_unavailable key={} reason=\"{}\"", key,
             DescribeRecordingError(started.error()));
    return std::nullopt;
  }
  return *std::move(started);
}
#endif

}  // namespace augusta::physics

#include "augusta/protocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

#include <google/protobuf/arena.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message_lite.h>
#include <utf8_range.h>

#include "augusta.pb.h"
#include "capture_records.h"
#include "decoder.h"
#include "encoder.h"
#include "framing.h"

namespace augusta::protocol {

namespace {

// The first block of each call's arena, on the stack: more than an
// Authoritative State or a Commands message as large as the protocol allows
// builds or parses into, so neither touches the heap but for an encode's
// payload. A larger message (a Replay list) grows the arena there.
constexpr std::size_t kArenaBlockSize = 4096;

// An arena local to one call of the codec (ADR-0007): the messages the
// call builds or parses live on it and go with it, and none reaches the API.
class CallArena {
 public:
  template <typename Message>
  Message& Create() {
    return *google::protobuf::Arena::Create<Message>(&arena_);
  }

 private:
  alignas(std::max_align_t) std::array<char, kArenaBlockSize> block_;
  google::protobuf::Arena arena_{block_.data(), block_.size()};
};

// message's bytes: the one heap allocation of an encode.
BytesWire Serialize(const google::protobuf::MessageLite& message) {
  BytesWire bytes(message.ByteSizeLong());
  message.SerializeWithCachedSizesToArray(reinterpret_cast<std::uint8_t*>(bytes.data()));
  return bytes;
}

// What read maps payload's Envelope (augusta.Message or augusta.CaptureRecord)
// to, or why it is none of this build: kMalformed if protobuf cannot parse it,
// kUnknownType if it sets no member this build knows, else the first value the
// mapping refused.
template <typename Wire, typename Envelope, typename ReadEnvelope>
std::expected<Wire, DecodeError> DecodeEnvelope(std::span<const std::byte> payload, ReadEnvelope read) {
  CallArena arena;
  auto& envelope = arena.Create<Envelope>();
  if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      !envelope.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    return std::unexpected(DecodeError::kMalformed);
  }
  Inbound in;
  std::optional<Wire> wire = read(envelope, in);
  if (!wire.has_value()) {
    return std::unexpected(DecodeError::kUnknownType);
  }
  if (in.Error().has_value()) {
    return std::unexpected(*in.Error());
  }
  return std::move(*wire);
}

}  // namespace

std::expected<BytesWire, EncodeError> Encode(const MessageWire& message) {
  CallArena arena;
  auto& encoded = arena.Create<pb::Message>();
  Outbound out;
  std::visit(Encoder{.message = encoded, .out = out}, message);
  if (out.Error().has_value()) {
    return std::unexpected(*out.Error());
  }
  return Serialize(encoded);
}

std::expected<void, EncodeError> Readdress(BytesWire& payload, const AuthoritativeStateWire& recipient) {
  const std::optional<std::span<const std::byte>> shared = SharedFieldsOf(payload);
  if (!shared.has_value()) {
    return std::unexpected(EncodeError::kNotAnAuthoritativeState);
  }
  CallArena arena;
  auto& own = arena.Create<pb::AuthoritativeState>();
  WriteOwn(recipient, own);
  const std::size_t own_size = own.ByteSizeLong();
  const auto length = static_cast<std::uint32_t>(shared->size() + own_size);
  using google::protobuf::io::CodedOutputStream;
  BytesWire readdressed(CodedOutputStream::VarintSize32(kAuthoritativeStateTag) +
                        CodedOutputStream::VarintSize32(length) + length);
  auto* at = reinterpret_cast<std::uint8_t*>(readdressed.data());
  at = CodedOutputStream::WriteVarint32ToArray(kAuthoritativeStateTag, at);
  at = CodedOutputStream::WriteVarint32ToArray(length, at);
  at = std::ranges::transform(*shared, at, [](std::byte byte) { return static_cast<std::uint8_t>(byte); }).out;
  own.SerializeWithCachedSizesToArray(at);
  payload = std::move(readdressed);
  return {};
}

MessageTypeWire TypeOf(const MessageWire& message) {
  struct Type {
    MessageTypeWire operator()(const JoinRequestWire& /*wire*/) const { return MessageTypeWire::kJoinRequest; }
    MessageTypeWire operator()(const JoinAcceptedWire& /*wire*/) const { return MessageTypeWire::kJoinAccepted; }
    MessageTypeWire operator()(const JoinRefusedWire& /*wire*/) const { return MessageTypeWire::kJoinRefused; }
    MessageTypeWire operator()(const CommandsWire& /*wire*/) const { return MessageTypeWire::kCommands; }
    MessageTypeWire operator()(const AuthoritativeStateWire& /*wire*/) const {
      return MessageTypeWire::kAuthoritativeState;
    }
    MessageTypeWire operator()(const LobbyWire& /*wire*/) const { return MessageTypeWire::kLobby; }
    MessageTypeWire operator()(const ReadyWire& /*wire*/) const { return MessageTypeWire::kReady; }
    MessageTypeWire operator()(const MatchStartWire& /*wire*/) const { return MessageTypeWire::kMatchStart; }
    MessageTypeWire operator()(const MatchEndWire& /*wire*/) const { return MessageTypeWire::kMatchEnd; }
    MessageTypeWire operator()(const ShotWire& /*wire*/) const { return MessageTypeWire::kShot; }
    MessageTypeWire operator()(const HitConfirmationWire& /*wire*/) const { return MessageTypeWire::kHitConfirmation; }
    MessageTypeWire operator()(const DeathWire& /*wire*/) const { return MessageTypeWire::kDeath; }
    MessageTypeWire operator()(const ReplayListRequestWire& /*wire*/) const {
      return MessageTypeWire::kReplayListRequest;
    }
    MessageTypeWire operator()(const ReplayListWire& /*wire*/) const { return MessageTypeWire::kReplayList; }
    MessageTypeWire operator()(const ReplayRequestWire& /*wire*/) const { return MessageTypeWire::kReplayRequest; }
    MessageTypeWire operator()(const ReenactRequestWire& /*wire*/) const { return MessageTypeWire::kReenactRequest; }
#if AUGUSTA_DEBUG_TOOLS
    MessageTypeWire operator()(const DebugSubscribeWire& /*wire*/) const { return MessageTypeWire::kDebugSubscribe; }
    MessageTypeWire operator()(const ShotJudgementWire& /*wire*/) const { return MessageTypeWire::kShotJudgement; }
#endif
  };
  return std::visit(Type{}, message);
}

MessageTypeWire TypeOf(std::span<const std::byte> payload) {
  google::protobuf::io::CodedInputStream input(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                               static_cast<int>(std::min(payload.size(), kMaxTagBytes)));
  return static_cast<MessageTypeWire>(input.ReadTag() >> kTagTypeBits);
}

std::expected<MessageWire, DecodeError> Decode(std::span<const std::byte> payload) {
  return DecodeEnvelope<MessageWire, pb::Message>(payload, ReadBody);
}

std::expected<BytesWire, EncodeError> EncodeCaptureRecord(const CaptureRecordWire& record) {
  CallArena arena;
  auto& encoded = arena.Create<pb::CaptureRecord>();
  Outbound out;
  std::visit(RecordEncoder{.record = encoded, .out = out}, record);
  if (out.Error().has_value()) {
    return std::unexpected(*out.Error());
  }
  return Serialize(encoded);
}

CaptureRecordTypeWire TypeOf(const CaptureRecordWire& record) {
  struct Type {
    CaptureRecordTypeWire operator()(const CaptureHeaderWire& /*wire*/) const { return CaptureRecordTypeWire::kHeader; }
    CaptureRecordTypeWire operator()(const CapturedJoinWire& /*wire*/) const { return CaptureRecordTypeWire::kJoin; }
    CaptureRecordTypeWire operator()(const CapturedCommandWire& /*wire*/) const {
      return CaptureRecordTypeWire::kCommand;
    }
    CaptureRecordTypeWire operator()(const CapturedLeaveWire& /*wire*/) const { return CaptureRecordTypeWire::kLeave; }
    CaptureRecordTypeWire operator()(const CapturedDeathWire& /*wire*/) const { return CaptureRecordTypeWire::kDeath; }
    CaptureRecordTypeWire operator()(const CapturedMatchEndWire& /*wire*/) const {
      return CaptureRecordTypeWire::kMatchEnd;
    }
  };
  return std::visit(Type{}, record);
}

std::expected<CaptureRecordWire, DecodeError> DecodeCaptureRecord(std::span<const std::byte> payload) {
  return DecodeEnvelope<CaptureRecordWire, pb::CaptureRecord>(payload, ReadRecord);
}

std::string_view DescribeDecodeError(DecodeError error) {
  switch (error) {
    case DecodeError::kMalformed:
      return "payload does not parse";
    case DecodeError::kUnknownType:
      return "no message type this build knows";
    case DecodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
    case DecodeError::kReservedBits:
      return "flags field has a bit that is none of its flags";
    case DecodeError::kOutOfRange:
      return "number outside its grid or wider than its field";
    case DecodeError::kFieldTooLong:
      return "string or list field longer than allowed";
  }
  return "unknown decode error";
}

std::string_view DescribeEncodeError(EncodeError error) {
  switch (error) {
    case EncodeError::kFieldTooLong:
      return "string or list field longer than the protocol carries";
    case EncodeError::kReservedBits:
      return "flags field has a bit that is none of its flags";
    case EncodeError::kInvalidEnum:
      return "field holds a value its enumeration lacks";
    case EncodeError::kNotAnAuthoritativeState:
      return "payload to readdress is not an authoritative state's";
    case EncodeError::kNotUtf8:
      return "string field is not utf-8";
  }
  return "unknown encode error";
}

bool IsUtf8(std::string_view text) { return utf8_range_IsValid(text.data(), text.size()) != 0; }

}  // namespace augusta::protocol

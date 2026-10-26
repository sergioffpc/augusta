#ifndef AUGUSTA_PROTOCOL_CAPTURE_RECORDS_H_
#define AUGUSTA_PROTOCOL_CAPTURE_RECORDS_H_

#include <optional>

#include "augusta.pb.h"
#include "augusta/protocol.h"
#include "decoder.h"
#include "encoder.h"
#include "generated_messages.h"

/// \file
/// A Match capture's records (ADR-0050), each its augusta.CaptureRecord member,
/// mapped as the messages are (encoder.h, decoder.h): a captured Command's
/// command is a Commands message's augusta.Command, its sequence left 0.
/// Private to the module.
namespace augusta::protocol {

/// One overload per record: the envelope's member, then its fields.
struct RecordEncoder {
  pb::CaptureRecord& record;
  Outbound& out;

  void operator()(const CaptureHeaderWire& wire) const;
  void operator()(const CapturedJoinWire& wire) const;
  void operator()(const CapturedCommandWire& wire) const;
  void operator()(const CapturedLeaveWire& wire) const;
  void operator()(const CapturedDeathWire& wire) const;
  void operator()(const CapturedMatchEndWire& wire) const;
};

/// record's member, or nullopt when it sets none this build knows.
std::optional<CaptureRecordWire> ReadRecord(const pb::CaptureRecord& record, Inbound& in);

}  // namespace augusta::protocol

#endif  // AUGUSTA_PROTOCOL_CAPTURE_RECORDS_H_

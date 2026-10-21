#ifndef AUGUSTA_CLIENT_RUN_H_
#define AUGUSTA_CLIENT_RUN_H_

#include "augusta/uuid.h"

/// \file
/// The start of one Run of augustac (GLOSSARY.md): its Run ID, and the log it
/// leaves beside the executable (ADR-0027); and its Session's ID in that log
/// once it is admitted.
namespace augusta::client {

/// Takes the Run's ID and starts its log: every line from now on carries `run=`
/// (ADR-0029) and also reaches `logs/augustac-<run-id>.log` beside the
/// executable, or, if that file cannot be opened, only the console, with a
/// warning saying why. Then writes the Run's first lines: the version and
/// build (augusta/version.h), then the machine (augusta/hardware.h); the GPU
/// and its driver follow from the Renderer once it chooses one. Call once, after
/// logging::Init and before the config is read, so a config failure is named by
/// the Run too.
uuid::Uuid BeginRun();

/// While it lives, every line carries `session=<session>` (ADR-0029): held by
/// ClientRuntime from the admission it sees to the Session's end, so the
/// client's lines map to the server's about the same Session.
class SessionLogContext {
 public:
  explicit SessionLogContext(const uuid::Uuid& session);
  ~SessionLogContext();

  SessionLogContext(const SessionLogContext&) = delete;
  SessionLogContext& operator=(const SessionLogContext&) = delete;
  SessionLogContext(SessionLogContext&&) = delete;
  SessionLogContext& operator=(SessionLogContext&&) = delete;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_RUN_H_

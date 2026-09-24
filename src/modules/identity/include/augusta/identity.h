#ifndef AUGUSTA_IDENTITY_H_
#define AUGUSTA_IDENTITY_H_

#include <cstdint>

// augusta::identity names what client and server both call a player by, in
// the engine's own terms: the harness, the server's Match and replication, and
// the presentation all speak of a Session ID through this module, never
// through the protocol's SessionIdWire, which exists only where a message is
// encoded or decoded (harness_wire, server/wire).
namespace augusta::identity {

/// The Authoritative server's name for one connected player (CONTEXT.md,
/// "Session ID"), assigned when it admits the join. Distinct from the
/// transport's handle for the connection, and not a credential.
enum class SessionId : std::uint32_t {};

}  // namespace augusta::identity

#endif  // AUGUSTA_IDENTITY_H_

# Networking Protocol: Message Catalogue and Reliability Split

Extends [ADR-0007](0007-serialization-format.md) (custom binary format) with the
shape of a message and which messages the transport delivers reliably. The
protocol is the shared `augusta_protocol` module: a pure codec with no socket,
clock or state, used by both client and server (ADR-0006).

**Wire shape.** One message is one transport payload: a one-byte `MessageType`
followed by that type's fields, fixed-width and little-endian; a string is a
one-byte length and its bytes. Enumerated fields start at 1, so a zeroed byte is
never a valid value. There is no length prefix on the payload itself, since the
transport already frames messages.

**Untrusted input.** `Decode` never throws: it returns
`std::expected<Message, DecodeError>` (ADR-0033) where the error is `kEmpty`,
`kUnknownType`, `kTruncated`, `kTrailingBytes`, `kInvalidEnum` or `kFieldTooLong`.
It checks every length against the bytes remaining before allocating, and rejects
a payload with bytes left over once its message is complete, so a message has
exactly one encoding. Every receiver drops what fails to decode and logs it; a
malformed message never changes state.

**Reliability split.** The sender names a `networking::Reliability` for every
send (ADR-0003's transport offers both). A message that must arrive, and whose
loss would leave the two sides disagreeing, is reliable; a message a newer one
supersedes is unreliable.

| Message | Direction | Reliability | Fields |
| --- | --- | --- | --- |
| Join request | client → server | reliable | engine version |
| Join accepted | server → client | reliable | session ID |
| Join refused | server → client | reliable | reason: version mismatch, match full |

Later messages (commands, Authoritative State updates) are added here as they
are built; per-tick traffic is unreliable and carries recent commands redundantly
so one lost datagram does not drop input.

**Joining.** A connection is accepted at the transport unconditionally, because a
refusal is itself a message and needs a connection to travel on. The client's
first message is a Join request carrying its engine version; the server admits it
only on an exact match with its own and while the match holds fewer than 8
players, checking the version first so a client that can never play here is not
told "full". After a refusal the client closes the connection.

**Session ID.** The server names each admitted player with a session ID it
generates itself: a counter that is never reused, deliberately unrelated to the
transport's connection handle, so identifying a player inside a message does not
depend on how the connection is represented. It identifies; it does not
authenticate. The server tells senders apart by connection, so a guessed ID grants
nothing, and it is kept out of logs per ADR-0029 regardless.

## Considered Options

- **Refuse at the transport instead of with a message**: rejected — the transport
  gives the client only "connection closed", so the player could not be told why
  (US-01 asks for the reason).
- **Version as a number or hash instead of the engine version string**: rejected
  for now — the string is what `augusta::EngineVersion()` already is, exact
  equality is the rule, and 32 bytes at join time cost nothing.

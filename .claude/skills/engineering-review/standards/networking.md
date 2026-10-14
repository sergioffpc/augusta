# Networking and protocol standard

**Objective:** messages are encoded and decoded identically by both ends, every
inbound byte is treated as untrusted, and connection lifecycle and delivery
semantics are handled for every order of events. Load for code that sends,
receives, encodes or decodes data across a process boundary. Untrusted-input
questions in [security.md](security.md) apply too.

## Questions

- **Wire format:** is each message's layout defined once and used by both
  encoder and decoder? Are endianness, field widths, alignment and padding
  explicit? How are versions or unknown message types handled?
- **Decoding:** is every length, count and enum checked against bounds before
  use? Is a truncated or oversized message rejected without partial effect? Does
  decoding of a valid message round-trip exactly?
- **Delivery semantics:** for each message type, what does the code assume
  (reliable, ordered, at most once)? Does the channel provide it? What happens
  on loss, duplication, reordering, or a burst after a stall?
- **Sequence numbers and time:** are wrapping counters compared wrap-safely? Is
  a stale or future sequence rejected or handled?
- **Connection lifecycle:** what state exists per connection, and is it created
  and destroyed exactly once for every path (connect, timeout, reject, peer
  close, local close, process stop)? Can a message arrive for a connection
  already torn down?
- **Flow control:** what bounds outbound queues per peer? What happens to a slow
  or silent peer?
- **Encoding failures:** can the sender emit an invalid message, and is that
  caught before it leaves?

## Failure patterns

- Encoder and decoder disagreeing on a field width after one was changed.
- A count field used to size a loop before checking remaining bytes.
- Logic that needs ordering on an unreliable channel.
- Per-connection state leaked when a connection fails during handshake.
- A sequence comparison that breaks at wraparound.
- A message handled for a peer after its session ended.

## Evidence required

The two sides of the contract, cited, and the input or event order that breaks
it. For decoder issues, the exact byte layout of a triggering message.

## Verification

Round-trip and property tests over all message types; fuzzing the decoder with
sanitizers; an impaired-link run (loss, latency, reordering, duplication);
crafted malformed messages in a test in a throwaway worktree.

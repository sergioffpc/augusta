# protocol

`augusta_protocol`. Shared: linked by both the client and the server.

## Responsibility

The Networking Protocol: the messages client and server exchange, and the
capture's records, with their custom binary encoding. A pure codec - bytes in, a
message or a typed error out - that treats its input as untrusted, and whose
messages hold only its own plain `*Wire` types. An Authoritative State encoded
once is readdressed to each recipient by rewriting only its own fields' bytes.

`proto/augusta.proto`, alone in its directory, is the protocol's one Protocol
Buffers schema: every message under `augusta.Message` and every capture record
under `augusta.CaptureRecord`. The build generates its C++ (`augusta.pb.h`) into
the build tree, private to this module, which links the full protobuf runtime
`PRIVATE`.

## Not its responsibility

- Sockets and delivery: networking.
- Converting to the engine's types: link (`augusta_link_messages`).
- Which peer may send what, and what a message means for the Match: the
  receiver.

## Decisions

[ADR-0007](../../../docs/adr/0007-serialization-format.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md).

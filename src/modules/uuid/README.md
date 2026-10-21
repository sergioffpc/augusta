# uuid

`augusta_uuid`. Shared: linked by both the client and the server.

## Responsibility

The one value type the globally unique IDs are, the Run ID, the Session ID and
the Match ID: a UUIDv7, taken from one process-wide generator so later IDs sort
later, written in the canonical lowercase hyphenated form, compared and hashed.
Boost.Uuid takes them, hidden behind the module.

## Not its responsibility

- Which ID names what, and when one is taken: the client and the server.
- Carrying an ID on the wire or in a capture: protocol, link and capture, which
  take its 16 bytes.

## Decisions

[ADR-0035](../../../docs/adr/0035-boost-libraries.md).

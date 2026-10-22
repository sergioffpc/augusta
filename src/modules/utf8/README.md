# utf8

`augusta_utf8`. Shared: linked by both the client and the server.

## Responsibility

A path as UTF-8 text on every platform, for a log line, a message or a
comparison with a name from either, where `path::string()` would throw on
Windows for a character the code page lacks. Header-only; it depends on nothing
but the standard library, and nothing defines it again.

## Not its responsibility

- Checking that text from a peer is UTF-8: protocol (`protocol::IsUtf8`).
- Which paths a module writes, and how it quotes them: each module that writes
  one.

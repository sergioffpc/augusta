# version

`augusta_version`. Shared: linked by both the client and the server.

## Responsibility

The engine's version: what each executable prints for `--version` and logs at
startup, and what a client presents when it joins. And which build of it is
running: the PDB GUID and age (Windows) or build ID (Linux) that match a Crash
dump to its symbols, which the client logs on its first line.

## Not its responsibility

- Admitting or refusing a client by its version: the server.
- Choosing the version number: the release process.

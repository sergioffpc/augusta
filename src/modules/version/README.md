# version

`augusta_version`. Shared: linked by both the client and the server.

## Responsibility

The engine's version: what each executable prints for `--version` and logs at
startup, and what a client presents when it joins.

## Not its responsibility

- Admitting or refusing a client by its version: the server.
- Choosing the version number: the release process.

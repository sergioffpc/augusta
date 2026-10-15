# Shared Client/Server Codebase

Client and dedicated server build from a single shared codebase/core, rather
than separate implementations, to minimize simulation divergence between the
two.

The client builds and runs on Windows and Linux, x64 only, and the server on
Linux (Windows for development, NFR-04). Windows is the client's reference
platform: where the two differ, Windows' behaviour is the specification and
Linux follows it. One build graph holds both executables: the client is a CMake
option, `AUGUSTA_CLIENT`, on by default on either platform, which a build of the
server alone (its image, and CI's Linux jobs) turns off (ADR-0008).

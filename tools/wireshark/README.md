# Wireshark dissector

Wireshark reads the pcapng of decrypted messages a Development build writes when
its config's `debug.network.pcapng` names a directory (ADR-0076). Each packet is
an `exported_pdu` record naming the `augusta` dissector, which is
[augusta.lua](augusta.lua): a fixed shim that hands the payload to Wireshark's
built-in `protobuf` dissector as `augusta.Message`, read from the protocol's one
schema, [augusta.proto](../../src/modules/protocol/proto/augusta.proto). Every
message and field shows by name, and the shim never changes when the protocol
does. It needs Wireshark 4.4 or later, and no build.

## Install

```bash
tools/wireshark/install.sh
```

It runs on Linux, macOS and Git Bash on Windows, and is safe to run again. It
copies the shim into the personal Lua plugins folder
(`~/.local/lib/wireshark/plugins`, or `%APPDATA%\Wireshark\plugins` on Windows),
and adds the protocol's `src/modules/protocol/proto` directory to the default
profile's Protobuf search paths (`protobuf_search_paths` in
`~/.config/wireshark`, or `%APPDATA%\Wireshark`), with "Load all files" on.
`WIRESHARK_CONFIG_DIR` picks another personal configuration folder, as it does
for Wireshark. Then open a `.pcapng` the game wrote.

The search path points at this checkout, so Wireshark reads whatever `.proto`
its branch holds.

## One-off use

Without installing anything, from the repository root, name the shim with
`-X lua_script:` and the search path with `-o`. The search path must be
absolute: a relative one finds no schema. In Git Bash, `pwd -W` gives the
Windows path Wireshark reads.

```bash
schema="$(pwd)/src/modules/protocol/proto"
wireshark -X lua_script:tools/wireshark/augusta.lua \
  -o "uat:protobuf_search_paths:\"${schema}\",\"TRUE\"" \
  augustac-20261009T120000000Z.pcapng
tshark -X lua_script:tools/wireshark/augusta.lua \
  -o "uat:protobuf_search_paths:\"${schema}\",\"TRUE\"" \
  -r augustac-20261009T120000000Z.pcapng -V
```

Not both: with the shim installed, a second `-X lua_script:` of it registers
`augusta` twice, which Wireshark refuses with a Lua error.

## Which build's schema

A file's Section Header Block names the executable and engine version that wrote
it, `+dev` included, in `shb_userappl`: Wireshark's Capture File Properties
shows it as the application, as does `capinfos <file>`. Read the file with that
version's `.proto` (check out its tag). Nothing refuses a mismatch: an older
file still opens, and a message the schema has changed since decodes wrong or
shows as malformed.

The `augusta` tree item shows each message's direction, outbound or inbound,
from the writer's own point of view: `augusta.direction == "Inbound"` filters
what it received.

## Test

[tests/test_dissector.py](tests/test_dissector.py) runs `tshark` with the shim
over
[protocol_decode_seeds.pcapng](../../tests/fuzz/protocol_decode_seeds.pcapng),
which the C++ writer made of the protocol's fuzz seeds, one message of each kind
([tests/fuzz/README.md](../../tests/fuzz/README.md)). Every packet must decode
as the `augusta.Message` member its seed names, in its direction, with no
malformed or undecoded bytes. From `tools/wireshark`:

```bash
uv run --group wireshark pytest
```

Without `tshark` (and `capinfos`) on `PATH` it skips, unless
`AUGUSTA_REQUIRE_TSHARK` is set, as CI sets it.

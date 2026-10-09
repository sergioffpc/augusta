# Runtime Configuration: a YAML File Next to the Executable

`augustac` and `augustad` read their startup settings (the pack to load, the
public key to verify it against, the server address to connect to or listen on)
from one YAML file, not from a list of command-line arguments. By default each
executable reads a fixed-name file from its own directory: `augustac.yaml` and
`augustad.yaml`. The one setting they take from the command line is
`--config <file>`, which points them at another file (taken as given, relative
to the working directory). Besides it they accept only the GNU `--help` and
`--version`, which print the usage or the engine version to stdout and exit
successfully without reading any config (`--help` wins over the rest of the
line). Any other argument, including an old-style `augustac <pack> <key>`, fails
with the usage message instead of being half-honored. Keys are grouped into
sections by what they are about - `content` (pack, public key), `network`,
`logging`, `simulation` (server), `input` (client) - and a key is named by its
dotted path (`network.server_address`), which is what an error names too. A
section may hold another (`input.keys`, the client's keymap, whose entries are
the controls it rebinds). Values are strings; unknown keys and sections, missing
required keys, non-string values and a section that is not a mapping are errors,
so a misspelled optional key never silently falls back to its default. A value
that is a number (the server's tick rate) is read from its string, and one that
is not a number in its range (for the tick rate, a whole number of Hz from 1
to 255) is an error like any other. Relative paths start from a required
top-level `base_dir` key (itself relative to the config file's directory, so `.`
means the file's own), never from the working directory: the process starts the
same from anywhere, and where its content lives is always written down rather
than implied by where the executable sits. The parsing mechanism lives in the
shared `augusta_config` module (ADR-0006), using yaml-cpp (ADR-0025), and fails
startup through `std::expected` rather than throwing (ADR-0033). Each
executable's keys live on its own side of the split: `augusta_client_config`
(client-only, since its keymap names client Input's keys and controls) and
`augusta_server_config` (server-only), so the headless server never links or
includes client Input. A tool with a settings file of its own (`augusta-swarm`'s
`augusta-swarm.yaml`) reads it under the same rules through the module's schema
functions (`ReadConfigValues`, `RequirePath`, ...), but declares its keys and
their meaning itself, next to it: the module holds no key of a tool.

**Extended by ADR-0050**: `augustac` also takes `--reenact <capture>` and
`--player <n>`, together, which make one run a Captured player: a capture is
chosen for one run, not kept as a setting. An executable names such options of
its own to the shared parser (`augusta_client_config`'s
`kClientCommandLineOptions`), which lists them in its usage; every other
setting still comes from the file.

YAML because the repository already keeps its configuration in JSON and YAML (CI
workflows, Helm charts, cluster manifests, `vcpkg.json`, CMake presets): a third
format for one more kind of config is one more thing to know, not a better tool.

## Considered Options

- **TOML** (toml++): the nicer format to write by hand, but it would be a third
  configuration format in the repo for no capability the flat mapping needs.
- **A flat mapping of keys**: simplest to parse, but the file reads as one
  undifferentiated list, and a keymap - naturally a table - becomes one prefixed
  key per control. Sections say what each key is about.
- **JSON**: no comments, so a config can't explain its own keys.
- **A hand-rolled INI/key=value parser**: no new dependency, but it is parsing
  code to write, test and maintain, for a format nothing else in the repo uses.
- **Command-line arguments (status quo)**: the set of settings is growing
  (server address, listen address) and a k8s pod (`charts/augustad`) is
  configured through mounted files far more naturally than through container
  args.
- **No way to choose the file at all** (fixed location only): first choice, then
  reversed - running two configurations side by side, or keeping a config
  outside the build tree, needs a `--config`. An environment variable is not
  offered: one way to override is enough.

## Consequences

- A checkout needs the file next to the built executable
  (`build/x64-<preset>/src/{client,server}/`) before the first run.
  `config/augustac.example.yaml` and `config/augustad.example.yaml` are
  templates to copy there.
- The server container gets `augustad.yaml` from a mounted ConfigMap or Secret
  beside the binary once the chart wires it up (M0b); the image ships none.

# Runtime Configuration: a YAML File Next to the Executable

`augustac` and `augustad` read their startup settings (the pack to load, the
public key to verify it against, the server address to connect to or listen on)
from one YAML file, not from a list of command-line arguments. By default each
executable reads a fixed-name file from its own directory: `augustac.yaml` and
`augustad.yaml`. The one argument they accept is `--config <file>`, which points
them at another file (taken as given, relative to the working directory); any
other argument, including an old-style `augustac <pack> <key>`, fails with the
usage message instead of being half-honored. The format is a flat mapping of
keys to strings; unknown keys, missing required keys and non-string values are
errors, so a misspelled optional key never silently falls back to its default.
A value that is a number (the server's tick rate) is read from its string, and
one that is not a finite number in range is an error like any other.
Relative paths start from a required `base_dir` key (itself relative to the
config file's directory, so `.` means the file's own), never from the working
directory: the process starts the same from anywhere, and where its content
lives is always written down rather than implied by where the executable sits.
Parsing lives in the shared `augusta_config` module (ADR-0006), using yaml-cpp
(ADR-0025), and fails startup through `std::expected` rather than throwing
(ADR-0033).

YAML because the repository already keeps its configuration in JSON and YAML (CI
workflows, Helm charts, cluster manifests, `vcpkg.json`, CMake presets): a third
format for one more kind of config is one more thing to know, not a better
tool.

## Considered Options

- **TOML** (toml++): the nicer format to write by hand, but it would be a third
  configuration format in the repo for no capability the flat mapping needs.
- **JSON**: no comments, so a config can't explain its own keys.
- **A hand-rolled INI/key=value parser**: no new dependency, but it is parsing
  code to write, test and maintain, for a format nothing else in the repo uses.
- **Command-line arguments (status quo)**: the set of settings is growing (server
  address, listen address) and a k8s pod (`charts/augustad`) is configured
  through mounted files far more naturally than through container args.
- **No way to choose the file at all** (fixed location only): first choice, then
  reversed - running two configurations side by side, or keeping a config outside
  the build tree, needs a `--config`. An environment variable is not offered: one
  way to override is enough.

## Consequences

- A checkout needs the file next to the built executable
  (`build/x64-<preset>/src/{client,server}/`) before the first run.
  `config/augustac.example.yaml` and `config/augustad.example.yaml` are
  templates to copy there.
- The server container gets `augustad.yaml` from a mounted ConfigMap or Secret
  beside the binary once the chart wires it up (M0b); the image ships none.

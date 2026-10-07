# Scenario Composition Manifest

A map (ADR-0015), a character (ADR-0040), a cue sound (ADR-0020) and a script
(ADR-0022, ADR-0039) are all reusable authoring content: none of them is itself
"the thing that gets cooked into one pack." A **scenario** is one manifest file,
`<assets-root>/authoring/scenarios/<name>.yaml`, that composes them:
`augusta-pack <name>` resolves to that file (ADR-0030). It names everything it
composes by file, every path relative to `authoring/`, so nothing is found by a
folder convention or a fixed file name, and one file can be composed into any
number of scenarios.

```text
authoring/
  maps/firebase.usda
  characters/soldier.usda
  sounds/gunshot.wav ...
  scripts/parameters/rules_of_engagement.lua
  scripts/rules/last_man_standing.lua
  scenarios/firebase.yaml
```

```yaml
map: maps/firebase.usda
characters:
  soldier: characters/soldier.usda
sounds:
  gunshot: sounds/gunshot.wav
  hit_marker: sounds/hit_marker.wav
  hit_taken: sounds/hit_taken.wav
  death: sounds/death.wav
  match_won: sounds/match_won.wav
  match_lost: sounds/match_lost.wav
scripts:
  parameters: scripts/parameters/rules_of_engagement.lua
  rules: scripts/rules/last_man_standing.lua
```

- `map` names exactly one map's USD stage.
- `characters` maps zero or more characters' names to their USD stages. A
  character's **name**, what a client asks to play (ADR-0042) and what its blobs
  are addressed under, is its key: `soldier`. It is one lowercase word (a
  letter, then letters, digits or underscores), so it is a single path segment
  in the pack, and it is the scenario's, not the stage's: moving, renaming or
  converting a stage between `.usda` and `.usdc` never renames the character a
  client asks for, and one stage can be composed under different names.
- `sounds` maps every one of the client's cues to a mono PCM WAV file (ADR-0020,
  ADR-0031). It is required, and so is every cue: the client plays them all, so
  a scenario lacking one is refused when it is cooked rather than found silent
  in a Match. A file may be the sound of several cues.
- `scripts` maps each script role to a Lua file. `parameters` (ADR-0039) is
  required: the server reads it at startup. `rules` (ADR-0022) is optional:
  without it the scenario has no Game policy and the mechanism decides alone.
  The cooker packs each at its role's fixed name in the server pack,
  `parameters.lua` and `rules.lua`, which is where the server reads it, so a
  script file can be named for what it does (`last_man_standing.lua`) rather
  than for the slot it fills.

A key, cue or script role the cooker does not know is refused, not ignored, so a
misspelling is found when cooking. The cooker reads the map's stage, walks the
characters' stages, reads the cue sounds and scripts, and packs all of it into
one client/server pack pair: the pack's byte layout (ADR-0031) and the
client/server split (ADR-0019) are unaffected. A character's own prims are
addressed `<character name>/<prim path>` (e.g. `soldier/Character/Visual`) with
the prim's local transform and its stage's own up-axis/metersPerUnit correction
baked directly into its points, since - unlike a map's top-level nodes - a
character contributes no `Scene` node of its own to carry that correction as a
transform; the `Scene` blob stays map-only. usd-optimize and
usd-validation-nvidia run once per composed stage, the map then each character,
before the cook.

The manifest is YAML, not Lua, since composition is plain data with nothing to
compute - no expressions, no derived values the way the Parameters script's
stamina numbers are (ADR-0039). YAML is also what this repo already reaches for
outside Lua's own niche (ADR-0034's rationale for
`augustac.yaml`/`augustad.yaml` - "the repository already keeps its
configuration in JSON and YAML... a third format for one more kind of config is
one more thing to know").

**Still open**: UsdSkel skinning (ADR-0040's own still-open question) - the
example character's capsules are placeholder collision-derived shapes, not an
art asset with a rig.

## Considered Options

**A folder per scenario holding a fixed-name `manifest.yaml` and the scenario's
own scripts**: rejected. With the scripts out of it, the folder would hold one
fixed-name file and nothing else, and scripts kept inside a scenario's folder
cannot be shared with another scenario.

**Naming folders rather than files** (`map: maps/firebase`,
`sounds: sounds/firebase`, each folder holding fixed-name files such as
`map.usda` or `<cue>.wav`): rejected. Every fixed name is a convention an author
has to know and the cooker has to search for, a stage could be ambiguous
(`map.usda` beside `map.usdc`), and a sound could not be shared between cues or
scenarios without copying it.

**A list of sound files, each cue taken from its file's name**: rejected. It
keeps the fixed-name convention of the sounds folder in another form; a mapping
by cue says which file plays for which cue, lets one file serve several, and
makes a missing cue a missing key.

**Scripts as attributes of the map, in the manifest or in its stage**: rejected.
The rules (who spawns where, when the Match is won) and the Parameters are the
scenario's, not the map's: the same map composed with other rules is another
game mode, the flexibility the map/scenario split exists for. Behaviour that
belongs to the place itself, whatever the scenario (a trigger volume that
switches a light on), is a different concern, to be decided with the map when it
exists.

**A USD reference/payload in the map's stage naming its characters**: rejected
for the same reason: a map authored to be reusable across scenarios would
hard-code one scenario's character roster into itself.

**A list of character stages, each character named by its stage's path without
the extension** (`characters/soldier`): rejected. The name a client asks for
(ADR-0042) would change whenever an author moves or renames the file, and the
same stage could not be composed as two characters. A mapping by name, like the
sounds by cue, keeps the name the scenario's.

**Every character under `authoring/characters/` packed into every scenario**:
rejected. Declaring the characters costs nothing once a scenario declares its
map, and avoids every pack shipping the whole character library (ADR-0040).

**A Lua manifest (`scenario.lua`)**: rejected. Composition is pure data, so
Lua's sandboxed-execution machinery (ADR-0039: no `io`/`os`/randomness) buys
nothing here, and it would be a second, differently-shaped Lua convention next
to the Parameters script's own. YAML says the same thing with nothing to
sandbox.

**Folding composition into the Parameters script**: rejected. ADR-0039 gives it
a strict schema ("an unknown or missing key stops the server loading it");
folding composition into it would either loosen that schema or overload one file
with two unrelated jobs.

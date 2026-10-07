# Character Selection: Chosen in the Client Config, Validated at Join, Named by Path

A scenario can now compose more than one character (ADR-0041), and a character
is gameplay, not just appearance: its `Collider` ships in the server pack
(ADR-0040), so which character a player plays affects the simulation. This ADR
decides where a player's choice comes from, who has the final word on it, and
how every client learns every other player's character. It extends the message
catalogue of ADR-0038.

**Changed by ADR-0043**: a Lobby now comes before every match and no one joins
mid-match. Each player's character therefore rides in the Lobby updates and the
Match start message, never in an Authoritative State, and the Join checks gain
_match in progress_ before _lobby full_. How the choice is made (the client
config), validated (at join) and named (by its name in the manifest) is
unchanged.

**The choice comes from the client config.** `augustac.yaml` (ADR-0034) gains a
required `player.character` key naming a character by its name, the key the
manifest's `characters` maps to its stage (ADR-0041), e.g. `soldier`. There is
no selection screen, so the config is the only place a player can state a choice
today. (_Superseded by ADR-0043:_ this used to say there is no lobby either,
since a Match then had no pre-match waiting state; a Lobby now comes before
every Match, still without a selection screen.) A future UI only changes who
fills in the value, not the protocol beneath it.

**The server validates it at join and has the final word.** The Join request
carries the character name next to the engine version. The server admits the
player only if the name is one of its scenario's characters, and otherwise
refuses with a new Join refused reason, _unknown character_. Where this check
falls among the others is stated in ADR-0043. The character is fixed for the
life of the session: no message changes it after admission.

**Every message names a character by its name.** Join accepted tells the joining
player its own character, and each player in a Lobby update and in Match start
carries theirs (ADR-0043), each as the same string the Join request carries, at
most 64 bytes. These messages are reliable and sent only when the Lobby changes
or a Match starts, never per tick, so a string's size doesn't matter. A name
means the same in both packs whatever order the manifest lists them in, reads
the same in a log line, and is what Game policy sees of a player's character
(ADR-0022).

Both packs of a scenario are cooked from the same manifest, so the cooker
records its `characters` list in both the client and the server pack: the server
checks a join against it, and the client checks each character it is told about
against its own. A client told a character its pack does not list cannot draw
that player and stops, as it does for any character it cannot load.

**The client draws each player as its character.** The client loads the visual
mesh of each character it meets in the Lobby (ADR-0043), keyed by name and found
at `<character name>/Character/Visual` (ADR-0040), and the renderer draws each
remote player with the mesh its character names. This replaces the single fixed
mesh drawn for everyone, which stood in only while no selection existed.

## Considered Options

- **The server assigns characters (Game policy), with no client choice**:
  rejected, because a player should play what they picked. Assignment rules such
  as team-restricted characters can still arrive later as Game policy that
  refuses or narrows the requested choice, without changing where the request
  comes from.
- **The client requests and the server may override**: rejected for now as
  machinery with no rule to drive it yet. If policy ever needs to substitute a
  character, Join accepted already carries the player's own character, so the
  server could return a different one without a wire change.
- **A character index, its 1-based position in the pack's list, in one byte**:
  rejected. It saves up to 63 bytes per player, but only in the Lobby updates
  and Match start, which are rare, while it ties the meaning of every message to
  the manifest's order, makes logs name a number, and hands Game policy a number
  to look up instead of the character itself.
- **The character in every Authoritative State**: rejected. ADR-0043 settles
  every player's character before a Match's first tick, so nothing per tick
  needs it, and the one channel whose size matters would pay for it.
- **Fall back to the first character in the manifest on an unknown choice**:
  rejected, because the player would silently play something they didn't pick. A
  refusal with a reason matches how a version mismatch is already treated.

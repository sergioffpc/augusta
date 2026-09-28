# Character Selection: Chosen in the Client Config, Validated at Join, Replicated as an Index

A scenario can now compose more than one character (ADR-0041), and a character
is gameplay, not just appearance: its `Collider` ships in the server pack
(ADR-0040), so which character a player plays affects the simulation. This ADR
decides where a player's choice comes from, who has the final word on it, and
how every client learns every other player's character. It extends the message
catalogue of ADR-0038.

**Changed by ADR-0043**: a Lobby now comes before every match and no one joins
mid-match. The character index therefore rides in the Lobby updates and the
Match start message instead of every Authoritative State, and the Join checks
gain *match in progress* before *lobby full*. How the choice is made (the client
config), validated (at join) and addressed (by index into the pack's list) is
unchanged.

**The choice comes from the client config.** `augustac.yaml` (ADR-0034) gains a
required `player.character` key naming a character by its path relative to
`authoring/`, the same address the manifest's `characters` list uses (ADR-0041),
e.g. `characters/player`. There is no selection screen, so the config is the
only place a player can state a choice today. (_Superseded by ADR-0043:_ this
used to say there is no lobby either, since a Match then had no pre-match
waiting state; a Lobby now comes before every Match, still without a selection
screen.) A future UI only changes who fills in
the value, not the protocol beneath it.

**The server validates it at join and has the final word.** The Join request
carries the character path next to the engine version, as a string: it is sent
once per session, so its size doesn't matter, and it keeps the client from
depending on the manifest's order to name its own choice. The server admits the
player only if the path is one of its scenario's characters, and otherwise
refuses with a new Join refused reason, *unknown character*. Where this check
falls among the others is stated in ADR-0043. The character is fixed for the
life of the session: no message changes it after admission.

**Every other message names a character by index.** Both packs of a scenario are
cooked from the same manifest, so the cooker records its `characters` list, in
manifest order, in both the client and the server pack. A character's
**character index** is its 1-based position in that list, so a zeroed byte is
never valid, following ADR-0038's enum rule. That caps a scenario at 255
characters. Join accepted tells the joining player its own index, and each
player in a Lobby update and in Match start carries one (ADR-0043).
A client drops a message whose index is outside its own pack's list, as it drops
any message that does not decode.

**_Superseded by ADR-0043:_ the index rode in every Authoritative State, not in
a join event.** Now that no one joins a Match in progress, the Lobby updates and
Match start carry it reliably and the per-tick byte is gone. The reasoning held
while players could join mid-match: such a player first reached a client
through the Authoritative State, which is unreliable (ADR-0038). Putting the
index in that update made each update self-sufficient: a client never saw a
player whose character it didn't know, and no ordering between a reliable and
an unreliable channel had to be handled. The cost was at most 8 bytes per tick.

**The client draws each player as its character.** The client loads the visual
mesh of each character it meets in the Lobby (ADR-0043), keyed by index and
found at `<character path>/Character/Visual` (ADR-0040), and the renderer draws
each remote player with the mesh its index names. This replaces the single fixed
mesh drawn for everyone, which stood in only while no selection existed.

## Considered Options

- **The server assigns characters (Game policy), with no client choice**:
  rejected, because a player should play what they picked. Assignment rules such
  as team-restricted characters can still arrive later as Game policy that
  refuses or narrows the requested choice, without changing where the request
  comes from.
- **The client requests and the server may override**: rejected for now as
  machinery with no rule to drive it yet. If policy ever needs to substitute a
  character, Join accepted already carries the player's own index, so the server
  could return a different one without a wire change.
- **A reliable "player joined" message carrying the character instead of a
  per-tick index**: rejected. The unreliable Authoritative State can arrive
  before it, so the client would have to hold or hide a player whose character
  it doesn't know yet. That is an ordering problem the per-tick byte avoids.
- **The character path as a string in every Authoritative State**: rejected,
  because it costs about 20–30 bytes per player per tick on the one channel
  whose size matters.
- **Fall back to the first character in the manifest on an unknown choice**:
  rejected, because the player would silently play something they didn't pick.
  A refusal with a reason matches how a version mismatch is already treated.

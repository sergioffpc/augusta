# Lobby and Match Lifecycle: Fixed Player Count, Automatic Ready, No Mid-Match Joins

Until now the server ran one Match for its whole lifetime, and a player could
join it at any tick. That forced every client to handle a player it had never
seen before appearing in the middle of play. The client could not know that
player's character in advance, so it had to load whatever that player needed
while the match was running. This ADR replaces that model with a lobby in front
of each match. It changes parts of ADR-0042 and extends the message catalogue
of ADR-0038.

**A match has a fixed player count.** The scenario's Parameters (ADR-0039) set
how many players a match needs, at most `protocol::kMaxPlayers`. The server
decides this number and tells each client, as it does for every other
parameter.

**A lobby comes before every match.** An admitted player enters the Lobby, not
the match. Players can join and leave the Lobby freely until the match starts.
The server refuses a join once the Lobby already holds the player count.

**Ready is automatic.** A client is Ready once it has loaded everything it needs
to draw every player in the Lobby: each player's character is known from the
moment they are admitted (ADR-0042), so the client does its loading here and
never during play. The player never presses anything. The server numbers each
version of the Lobby's membership, and a client's Ready names the version it
loaded for. A Ready for an older version does not count, so a new arrival whose
character the client has not loaded yet makes that client not Ready again until
it catches up. A departure never does.

**The match starts when the Lobby is full and everyone is Ready.** At that point
the server closes the Lobby and sends one reliable Match start message. It lists
every player in the match with their character index, the Entity ID of the
body they control (ADR-0038) and its Spawn point. After
that, the set of players can only shrink.

**No one joins a match in progress.** A join that arrives while a match runs is
refused with a new Join refused reason, *match in progress*. The order of checks
is version, then character, then match in progress, then lobby full.

**A player who disconnects mid-match leaves it.** The server removes their body
from the simulation, and they stop appearing in the Authoritative State. Clients
remove them from presentation when they are gone from the Authoritative State.
There is no reconnecting to a match in progress. Whether the match continues
with fewer players is Game policy.

**When the match ends, everyone returns to the Lobby.** Game policy decides when
a match is over. The server then sends a reliable Match end message and reopens
the Lobby with the players still connected. Their Sessions, and so their
characters, carry over. Anyone who wants to leave disconnects. The next match
starts once the Lobby is full and Ready again, but never less than 5 seconds
after the previous one ended. Without that pause a Lobby that stayed full would
start the next match the instant the last one ended, with no time for anyone
to leave.

## Consequences

- **The character index leaves the Authoritative State.** ADR-0042 put it in
  every update only because a player could first appear mid-match through that
  unreliable channel. The Lobby updates and Match start now carry each player's
  index reliably, and the set of players is fixed before the first tick, so the
  per-tick byte is dropped.
- **The mid-match Roster in Join accepted goes away.** The Lobby's membership
  replaces it. The server sends it to everyone in the Lobby whenever it
  changes.
- **The Spawn point is assigned at match start, not at join.** The server keeps
  taking spawn points in order until Game policy says otherwise.

## Considered Options

- **Keep mid-match joins and load a newcomer's character on arrival**: rejected.
  Loading content during play can stall the frame. The client would also have
  to draw a player before it knows how, and every system would have to handle
  the player count changing while a match runs.
- **Ready as a button the player presses**: rejected. The only thing that makes
  a player unable to start is their client not having loaded the characters,
  and the client knows that better than the player does.
- **Load every character in the pack at startup, so a client is Ready as soon as
  it is admitted**: rejected. It loads characters no one in the match plays, and
  it would make Ready meaningless the moment loading ever depends on the Lobby.
- **Start with however many players are Ready, with a minimum**: rejected. A
  scenario is authored for a set number of players (its map, its spawn points,
  its rules), so a match should not start short-handed.
- **Choose the character in the Lobby, with a selection screen**: deferred. The
  client config remains the only place a player chooses (ADR-0042). A Lobby
  screen could fill in that value later without changing this lifecycle.

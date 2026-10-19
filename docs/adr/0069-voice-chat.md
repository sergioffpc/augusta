# Voice chat

Players of a team match (ADR-0068) cannot speak to each other: the client
captures no microphone, the protocol carries no Speech, and the only sounds are
the cues and the world's. This ADR decides how players talk, who hears them and
how Speech travels. It takes effect in the change that brings Voice chat, which
amends ADR-0028 (the microphone), ADR-0034 (the settings) and ADR-0038 (the
message and its lane) to match.

**Radio and aloud.** A player speaks either by radio, to its whole Team wherever
they are, or aloud, to anyone near enough to hear, enemies included. Each has
its own push-to-talk, bound like any other control (US-20) and to a controller
button in VR. What is said by radio is also heard aloud around the speaker: a
player whispering into its radio beside an enemy gives itself away.

**Only in a Match.** Teams exist from Match start, so there is no Voice chat in
the Lobby or after Match end. A Replay viewer neither speaks nor hears it.

**How it sounds.** Aloud, Speech leaves the speaker's Character's head and is
spatialised like any sound of the world, through the HRTF and the Map's
acoustics (ADR-0010, ADR-0062): Speech behind a wall is muffled by it. Its range
is the scenario's, in its Parameters, 30 m by default. By radio, Speech is heard
without position, band-limited like a radio, with a squelch click at its start
and end; it is an earpiece, so no one beside a listener hears what it receives.

**The dead.** A Spectator hears its Team's radio and Speech aloud around the
body it watches, and speaks only to its dead teammates. Nothing a dead player
says reaches the living, by radio or aloud.

**Through the server.** Speech travels on the client's existing connection, as
an unreliable message on a lane of its own below the per-tick state, so Speech
never delays the game. The server routes it without decoding it: by radio to the
speaker's Team, aloud to the living within range and the Spectators watching
them, and from the dead to their dead teammates. A client therefore never
receives Speech it should not hear, whatever it does with what it receives. The
server refuses a frame larger than the codec's at its bit rate, more than 50
messages a second, or Speech from a player not in a Match, as any malformed
input (NFR-12).

**Opus.** Mono at 48 kHz, 20 ms frames at about 24 kbps, with forward error
correction for losses and discontinuous transmission for silence, one frame a
message. A listener holds each speaker's frames in an adaptive jitter buffer of
40 to 100 ms. At 8 players a client receives at most about 21 KB/s of Speech.

**The microphone.** Captured through miniaudio (ADR-0028), the device chosen in
the client config or the system's by default. A client without one logs why at
WARN and plays without sending Speech, as it does without an output device.
Speech is cleaned by RNNoise before it is encoded; there is no echo
cancellation, since players wear headsets.

**Push-to-talk only.** There is no voice activity detection, which would send
breathing and keyboards.

**Settings.** The client config holds Voice chat's volume, a switch turning it
off, and the microphone. Muting one player and showing who is speaking wait for
an interface that can name players; the squelch and the position of Speech say
who speaks meanwhile.

**Never captured.** Speech is not in the Match capture, for privacy and size, so
a Replay or a Reenactment is silent of it. Agents neither speak nor hear.

## Consequences

- **The client gains Opus and RNNoise**, and captures from a microphone.
- **The server relays audio**: at 8 players, at most about 1.3 Mbit/s of Speech
  upstream, never decoded.
- **The protocol gains a lane**: Speech below the per-tick state, so the Link's
  channel configuration changes.

## Considered Options

- **Proximity Voice chat in a free-for-all**: rejected. Voice chat only makes
  sense between teammates, and the free-for-all is gone (ADR-0068).
- **Peer to peer**: rejected. Lower latency, but it exposes players' addresses
  and takes from the server who hears what.
- **Radio only**: rejected. Speaking near an enemy would cost nothing.
- **Voice activity detection**: rejected. It sends breathing and keyboards.
- **Capturing Speech for Replays**: rejected. It records players' voices, and a
  capture's size would grow with them.
- **Dead players heard by the living**: rejected. A Spectator could call out
  what it watches.
- **Echo cancellation**: rejected for now. Headsets make it unnecessary.

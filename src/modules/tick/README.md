# tick

`augusta_tick`. Shared: linked by both the client and the server.

## Responsibility

The simulation's time: the Tick's number and the command Sequence's (one name
each, `tick::Tick` and `tick::Sequence`), and the fixed schedule the server's
Simulation loop and the client's Prediction loop both keep - when each Tick is
due, how it kept to time, the client's pacing and the render frame's blend
fraction. Pure: the caller passes the times in. It depends on nothing but the
standard library, and nothing else gives the Tick or the Sequence a type of its
own.

## Not its responsibility

- Owning the clock, sleeping and running the loops: the server's Simulation
  thread and the Harness's Prediction thread.
- Encoding a Tick or a Sequence: protocol.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md).

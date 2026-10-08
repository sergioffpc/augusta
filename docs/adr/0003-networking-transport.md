# Networking Transport: GameNetworkingSockets

Client-server communication runs over GameNetworkingSockets (Valve), on top of
UDP.

Unreliable messages go out without Nagle's batching delay
(`k_nSteamNetworkingSend_UnreliableNoNagle`): they are Commands and
Authoritative State updates, stale if held back for the transport's ~5 ms
window. Reliable messages keep Nagle. This is the transport module's own choice;
the Reliability a sender names still means only how a message is delivered.

Every send and receive reports what the local transport did with it (ADR-0033).
A message the transport accepted is sent; one it dropped because the peer's
connection could not take it (not connected, ending, or its queue full) is the
peer's outcome, and a reliable message dropped for a full queue ends that peer's
connection, since it can no longer be delivered as promised. A send, receive,
listen socket or poll group the local transport itself refuses is a `runtime`
failure (`transport_send_failed`, `transport_receive_failed`,
`listener_setup_failed`): it is never read as a message sent or an empty queue,
and the worker that meets it stops the runtime through its supervisor
(ADR-0005). A send holds the peer table's lock across the transport call, and
every close forgets its connection under that lock first, so a send never races
a close: an invalid handle is then the local transport's failure, never a peer
that just left. Reliable delivery stays the transport's: nothing above it
retries a send.

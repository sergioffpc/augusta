# Networking Transport: GameNetworkingSockets

Client-server communication runs over GameNetworkingSockets (Valve), on top of
UDP.

Unreliable messages go out without Nagle's batching delay
(`k_nSteamNetworkingSend_UnreliableNoNagle`): they are Commands and
Authoritative State updates, stale if held back for the transport's ~5 ms
window. Reliable messages keep Nagle. This is the transport module's own choice;
the Reliability a sender names still means only how a message is delivered.

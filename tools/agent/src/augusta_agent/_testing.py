"""A server in the script's own process, for this package's tests only.

    with Server(pack, public_key, player_count=2) as server:
        agent = augusta_agent.Agent(server.address, client_pack, "soldier")

Server verifies the server pack with the key and serves its scenario for
player_count players on a free loopback port until it is closed.
"""

from augusta_agent import _native

# A submodule of the native module, which Python cannot import by name.
Server = _native.testing.Server

__all__ = ["Server"]

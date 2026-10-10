"""Agents: players a Python script controls through Intents (ADR-0052).

A script loads the client pack once, then connects as many Agents as it wants
and tells each what to do; the Harness carries it out every tick, so the
script decides at its own pace and never on one. Each Agent reports Ready for
every Roster it is sent.

    pack = augusta_agent.load_pack("client.pack", "pack.pub")
    with augusta_agent.Agent("127.0.0.1:27015", pack, "soldier") as agent:
        agent.set_raw(augusta_agent.Command(move=(0.0, 0.0, -1.0)))
        print(agent.view().phase)

The native part, augusta_agent._native, is built by the root CMake build with
AUGUSTA_TOOLS into this directory (tools/README.md).
"""

from augusta_agent._native import Agent
from augusta_agent._native import AuthoritativeState
from augusta_agent._native import Body
from augusta_agent._native import Command
from augusta_agent._native import EntityBody
from augusta_agent._native import load_pack
from augusta_agent._native import Lobby
from augusta_agent._native import MatchPlayer
from augusta_agent._native import Pack
from augusta_agent._native import PackError
from augusta_agent._native import Phase
from augusta_agent._native import RosterEntry
from augusta_agent._native import Stance
from augusta_agent._native import View

__all__ = [
    "Agent",
    "AuthoritativeState",
    "Body",
    "Command",
    "EntityBody",
    "Lobby",
    "MatchPlayer",
    "Pack",
    "PackError",
    "Phase",
    "RosterEntry",
    "Stance",
    "View",
    "load_pack",
]

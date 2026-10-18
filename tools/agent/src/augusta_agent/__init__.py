"""Agents: players a Python script controls through Intents (ADR-0052).

A script loads the client pack once, then, inside its asyncio loop, connects
as many Agents as it wants and tells each what to do by setting an Intent on
each of its channels - movement (Move, MoveTo), aim (Look, AimAt) and trigger
(HoldFire, Reload, FireBursts) - or a Raw Command on all three; the Harness
carries them out every tick, so the script decides at its own pace and never
on one. Each Agent reports Ready for every Roster it is sent, unless made with
auto_ready=False. The script awaits each Agent's events and how each Intent
it set ended, and both raise AgentFailed once the Agent fails. agent.stats()
tells what its prediction, fire and link came to, and impair_links gives
every link of the process simulated latency, jitter, loss and reordering.

    async def main():
        pack = augusta_agent.load_pack("client.apak", "pack.pub")
        with augusta_agent.Agent("127.0.0.1:27015", pack, "soldier") as agent:
            agent.set_movement(augusta_agent.Move((0.0, 0.0, -1.0)))
            while True:
                print(await agent.next_event())

    asyncio.run(main())

The native part, augusta_agent._native, is built by the root CMake build with
AUGUSTA_TOOLS into this directory (tools/README.md).
"""

from augusta_agent._agent import Agent
from augusta_agent._agent import AgentFailed
from augusta_agent._agent import AimIntent
from augusta_agent._agent import Event
from augusta_agent._agent import MovementIntent
from augusta_agent._agent import TriggerIntent
from augusta_agent._native import AimAt
from augusta_agent._native import AuthoritativeState
from augusta_agent._native import Body
from augusta_agent._native import BodyPart
from augusta_agent._native import Command
from augusta_agent._native import ConnectionStats
from augusta_agent._native import Death
from augusta_agent._native import EntityBody
from augusta_agent._native import FireBursts
from augusta_agent._native import HitConfirmation
from augusta_agent._native import HoldFire
from augusta_agent._native import impair_links
from augusta_agent._native import IntentEnding
from augusta_agent._native import LinkFaults
from augusta_agent._native import load_pack
from augusta_agent._native import Lobby
from augusta_agent._native import Look
from augusta_agent._native import MatchEnd
from augusta_agent._native import MatchPlayer
from augusta_agent._native import MatchStart
from augusta_agent._native import Move
from augusta_agent._native import MoveTo
from augusta_agent._native import NetcodeStats
from augusta_agent._native import Pack
from augusta_agent._native import PackError
from augusta_agent._native import Phase
from augusta_agent._native import Reload
from augusta_agent._native import RosterEntry
from augusta_agent._native import Shot
from augusta_agent._native import Stance
from augusta_agent._native import Stats
from augusta_agent._native import View

__all__ = [
    "Agent",
    "AgentFailed",
    "AimAt",
    "AimIntent",
    "AuthoritativeState",
    "Body",
    "BodyPart",
    "Command",
    "ConnectionStats",
    "Death",
    "EntityBody",
    "Event",
    "FireBursts",
    "HitConfirmation",
    "HoldFire",
    "IntentEnding",
    "LinkFaults",
    "Lobby",
    "Look",
    "MatchEnd",
    "MatchPlayer",
    "MatchStart",
    "Move",
    "MoveTo",
    "MovementIntent",
    "NetcodeStats",
    "Pack",
    "PackError",
    "Phase",
    "Reload",
    "RosterEntry",
    "Shot",
    "Stance",
    "Stats",
    "TriggerIntent",
    "View",
    "impair_links",
    "load_pack",
]

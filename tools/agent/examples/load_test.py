"""Fills a server with Agents that play until they have seen some Match ends.

Connects one Agent, then, once the server has told it the scenario's Player
count, the rest. In a Match, each wanders near where it spawned, aims at the
nearest living other player and fires in Bursts. Exits 0 once every Agent has
seen the Match ends asked for, 1 when one fails or the timeout passes first;
either way, prints every Agent's stats.

Each Agent wanders a leg at a time: a random heading, sprint and stance for
one to three seconds, never more than a few metres from its spawn, since it
knows nothing of the Map's edges; strayed, it heads back there. Every random
choice comes from the seed, the next Agent's from the next number.

Usage (from tools/, after `uv sync` and a build with AUGUSTA_TOOLS):
    uv run python agent/examples/load_test.py --server 127.0.0.1:27015 \
        --pack <client.pack> --public-key <pack.pub> --matches 3
"""

import argparse
import asyncio
from collections.abc import Sequence
import math
import random
import sys

import augusta_agent

# How often an Agent looks again at what it has been told, to decide.
DECIDE_S = 0.05
# How far it may stray from its spawn, in metres, before it heads back.
LEASH_M = 3.0
# A leg lasts one to three seconds; of every ten legs, three sprint, and seven
# stand, two crouch and one goes prone.
LEG_SECONDS = (1, 2, 3)
SPRINTING = 0.3
STANCES = (
    augusta_agent.Stance.STANDING,
    augusta_agent.Stance.CROUCHING,
    augusta_agent.Stance.PRONE,
)
STANCE_WEIGHTS = (7, 2, 1)


class Policy:
    """What one Agent decides in a Match, from its view and its own seed."""

    def __init__(self, agent: augusta_agent.Agent, seed: int):
        self._agent = agent
        self._random = random.Random(seed)
        self._match = 0
        self._spawn: tuple[float, float, float] | None = None
        self._leg_ends = 0.0
        self._sprint = False
        self._stance = augusta_agent.Stance.STANDING
        self._heading_back = False
        self._aimed_at: int | None = None
        self._firing = False

    async def play(self) -> None:
        """Decides every DECIDE_S, until cancelled or the Agent fails."""
        loop = asyncio.get_running_loop()
        while self._agent.failure is None:
            self._decide(self._agent.view(), loop.time())
            await asyncio.sleep(DECIDE_S)

    def _decide(self, view: augusta_agent.View, now: float) -> None:
        # Outside a Match and once dead, it decides nothing: the Harness
        # holds its Intents, sending nothing they say, until it plays again.
        own = view.own_entity
        if view.authoritative is None or own is None or own in view.dead:
            return
        bodies = {b.entity: b.body for b in view.authoritative.bodies}
        if own not in bodies:
            return
        if view.matches_started != self._match:
            self._start_match(view)
        self._wander(now)
        self._head_back_if_strayed(view.own.position)
        self._take_on_nearest(view, own, bodies)

    def _start_match(self, view: augusta_agent.View) -> None:
        # The last Match's leg and way back lead nowhere in this one, and its
        # target is gone.
        self._match = view.matches_started
        self._spawn = next(
            (p.spawn for p in view.match_players if p.session == view.session),
            None,
        )
        self._leg_ends = 0.0
        self._heading_back = False
        self._aimed_at = None

    def _wander(self, now: float) -> None:
        if self._heading_back or now < self._leg_ends:
            return
        yaw = self._random.uniform(-math.pi, math.pi)
        self._sprint = self._random.random() < SPRINTING
        self._stance = self._random.choices(STANCES, STANCE_WEIGHTS)[0]
        self._leg_ends = now + self._random.choice(LEG_SECONDS)
        # Yaw 0 looks down -Z and grows to the left.
        direction = (-math.sin(yaw), 0.0, -math.cos(yaw))
        self._agent.set_movement(
            augusta_agent.Move(
                direction, sprint=self._sprint, stance=self._stance
            )
        )

    def _head_back_if_strayed(
        self, position: tuple[float, float, float]
    ) -> None:
        spawn = self._spawn
        if self._heading_back or spawn is None:
            return
        if (
            math.hypot(position[0] - spawn[0], position[2] - spawn[2])
            <= LEASH_M
        ):
            return
        # Ending the leg under way, until it is back or can get no nearer.
        heading = self._agent.set_movement(
            augusta_agent.MoveTo(
                spawn, sprint=self._sprint, stance=self._stance
            )
        )
        heading.add_done_callback(self._back)
        self._heading_back = True

    def _back(self, heading: asyncio.Future) -> None:
        if ended_on_its_own(heading):
            self._heading_back = False
            self._leg_ends = 0.0

    def _take_on_nearest(
        self,
        view: augusta_agent.View,
        own: int,
        bodies: dict[int, augusta_agent.Body],
    ) -> None:
        position = bodies[own].position
        living = [
            (math.dist(body.position, position), entity)
            for entity, body in bodies.items()
            if entity != own and entity not in view.dead
        ]
        if not living:
            if self._firing:
                self._agent.clear_trigger()
                self._firing = False
            return
        target = min(living)[1]
        if target != self._aimed_at:
            aiming = self._agent.set_aim(augusta_agent.AimAt(target))
            aiming.add_done_callback(
                lambda ended: self._target_gone(target, ended)
            )
            self._aimed_at = target
        if not self._firing:
            self._agent.set_trigger(augusta_agent.FireBursts())
            self._firing = True

    def _target_gone(self, target: int, aiming: asyncio.Future) -> None:
        if self._aimed_at == target and ended_on_its_own(aiming):
            self._aimed_at = None


def ended_on_its_own(ending: asyncio.Future) -> bool:
    """Whether an Intent ended other than REPLACED, or its Agent did."""
    return (
        ending.cancelled()
        or ending.exception() is not None
        or ending.result() != augusta_agent.IntentEnding.REPLACED
    )


async def see_match_ends(agent: augusta_agent.Agent, matches: int) -> None:
    """Returns once agent has seen matches Match ends; raises if it fails."""
    ended = 0
    while ended < matches:
        if isinstance(await agent.next_event(), augusta_agent.MatchEnd):
            ended += 1


async def play_agent(
    agent: augusta_agent.Agent, seed: int, matches: int
) -> None:
    """Plays agent until it has seen matches Match ends."""
    playing = asyncio.create_task(Policy(agent, seed).play())
    try:
        await see_match_ends(agent, matches)
    finally:
        playing.cancel()


async def connect_all(
    args: argparse.Namespace,
    pack: augusta_agent.Pack,
    agents: list[augusta_agent.Agent],
) -> None:
    """Connects the scenario's Player count of Agents.

    Each is appended to agents as it connects, so the caller closes every one
    connected even if this is cut short.
    """
    agents.append(augusta_agent.Agent(args.server, pack, args.character))
    while (player_count := agents[0].view().player_count) is None:
        if agents[0].failure is not None:
            raise augusta_agent.AgentFailed(agents[0].failure)
        await asyncio.sleep(DECIDE_S)
    while len(agents) < player_count:
        agents.append(augusta_agent.Agent(args.server, pack, args.character))


async def run(args: argparse.Namespace, pack: augusta_agent.Pack) -> int:
    """Plays the run to its end; returns the process's exit code."""
    agents: list[augusta_agent.Agent] = []
    code = 1
    try:
        async with asyncio.timeout(args.timeout):
            await connect_all(args, pack, agents)
            async with asyncio.TaskGroup() as group:
                for index, agent in enumerate(agents):
                    group.create_task(
                        play_agent(agent, args.seed + index, args.matches)
                    )
        code = 0
    except TimeoutError:
        print(f"timed out after {args.timeout} s", file=sys.stderr)
    except augusta_agent.AgentFailed as failed:
        print(failed, file=sys.stderr)
    except ExceptionGroup as group:
        failed, rest = group.split(augusta_agent.AgentFailed)
        if rest is not None:
            raise rest from None
        for error in failed.exceptions:
            print(error, file=sys.stderr)
    finally:
        for index, agent in enumerate(agents):
            print_stats(index, agent)
            agent.close()
    return code


def print_stats(index: int, agent: augusta_agent.Agent) -> None:
    """Prints one line of what agent's prediction, fire and link came to."""
    stats = agent.stats()
    netcode, connection = stats.netcode, stats.connection
    link = (
        " connection=-"
        if connection is None
        else f" ping_ms={connection.ping_ms}"
        f" quality_local={connection.quality_local:.2f}"
        f" quality_remote={connection.quality_remote:.2f}"
        f" in_bytes_per_sec={connection.in_bytes_per_sec:.0f}"
        f" out_bytes_per_sec={connection.out_bytes_per_sec:.0f}"
        f" max_jitter_us={connection.max_jitter_us}"
        f" pending_bytes={connection.pending_bytes}"
    )
    print(
        f"agent={index} match_ticks={netcode.match_ticks}"
        f" corrections={netcode.corrections}"
        f" largest_correction_m={netcode.largest_correction_m:.3f}"
        f" rounds_fired={netcode.rounds_fired}"
        f" hit_confirmations={netcode.hit_confirmations}"
        f"{link} failure={agent.failure or '-'}"
    )


def link_faults(args: argparse.Namespace) -> augusta_agent.LinkFaults | None:
    """The faults asked for on the command line, or None if none were."""
    faults = augusta_agent.LinkFaults(
        latency_ms=args.latency_ms,
        jitter_mean_ms=args.jitter_mean_ms,
        jitter_max_ms=args.jitter_max_ms,
        loss_percent=args.loss_percent,
        reorder_percent=args.reorder_percent,
        reorder_delay_ms=args.reorder_delay_ms,
    )
    asked = (
        args.latency_ms,
        args.jitter_mean_ms,
        args.jitter_max_ms,
        args.loss_percent,
        args.reorder_percent,
        args.reorder_delay_ms,
    )
    return faults if any(asked) else None


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    """The run's options, from argv or, if None, the command line."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", default="127.0.0.1:27015")
    parser.add_argument("--pack", required=True, help="the client pack")
    parser.add_argument(
        "--public-key", required=True, help="the key the pack is signed with"
    )
    parser.add_argument("--character", default="soldier")
    parser.add_argument(
        "--matches",
        type=int,
        default=1,
        help="the Match ends every Agent must see (default 1)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=300.0,
        help="seconds the run may take (default 300)",
    )
    parser.add_argument(
        "--seed", type=int, default=1, help="the first Agent's seed"
    )
    faults = parser.add_argument_group(
        "link faults",
        "simulated on every packet this process sends, the handshake's too",
    )
    faults.add_argument("--latency-ms", type=int, default=0)
    faults.add_argument("--jitter-mean-ms", type=int, default=0)
    faults.add_argument("--jitter-max-ms", type=int, default=0)
    faults.add_argument("--loss-percent", type=float, default=0.0)
    faults.add_argument("--reorder-percent", type=float, default=0.0)
    faults.add_argument("--reorder-delay-ms", type=int, default=0)
    args = parser.parse_args(argv)
    if args.matches < 1:
        parser.error("--matches must be 1 or more")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    """Runs the load test; returns the process's exit code."""
    args = parse_args(argv)
    try:
        pack = augusta_agent.load_pack(args.pack, args.public_key)
    except augusta_agent.PackError as error:
        print(error, file=sys.stderr)
        return 1

    augusta_agent.impair_links(link_faults(args))
    try:
        return asyncio.run(run(args, pack))
    except KeyboardInterrupt:
        return 1
    finally:
        augusta_agent.impair_links(None)


if __name__ == "__main__":
    sys.exit(main())

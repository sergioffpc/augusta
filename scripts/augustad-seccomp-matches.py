#!/usr/bin/env python3
"""Plays the Matches scripts/augustad-seccomp.sh runs the server through.

The example scenario's Player count is 1 (tools/README.md, Running the Agent
examples), and a Match of one ends only when its player dies, which nothing in
it causes, or leaves: so load_test.py, which waits for Match ends, never
finishes against it. Here each Match is one Agent's own: it joins, walks to
and fro, sprinting, crouching and firing in Bursts, for --seconds, then
leaves, which ends the Match. The next Agent's Match starts only once the
server has ended the last one.

Exits 0 once every Agent has played its Match; 1 when one fails or the
timeout passes first.

Usage (from tools/, after `uv sync --group agent` and a build of the Agents'
native module):
    uv run python ../scripts/augustad-seccomp-matches.py \
        --server 127.0.0.1:7777 --pack <client.pack> \
        --public-key <pack.pub> --matches 2
"""

import argparse
import asyncio
from collections.abc import Sequence
import sys

import augusta_agent

# How long each leg of the walk to and fro lasts, in seconds.
LEG_S = 1.0


async def play_match(
    args: argparse.Namespace, pack: augusta_agent.Pack
) -> None:
    """Plays one Agent's Match for args.seconds, then leaves it."""
    with augusta_agent.Agent(args.server, pack, args.character) as agent:
        while not isinstance(
            await agent.next_event(), augusta_agent.MatchStart
        ):
            pass
        agent.set_trigger(augusta_agent.FireBursts())
        legs = int(args.seconds / LEG_S)
        for leg in range(legs):
            # Back and forth, so it stays near its spawn.
            forward = -1.0 if leg % 2 == 0 else 1.0
            agent.set_movement(
                augusta_agent.Move(
                    (0.0, 0.0, forward),
                    sprint=leg % 3 == 0,
                    stance=augusta_agent.Stance.CROUCHING
                    if leg % 4 == 3
                    else augusta_agent.Stance.STANDING,
                )
            )
            await asyncio.sleep(LEG_S)
            if agent.failure is not None:
                raise augusta_agent.AgentFailed(agent.failure)


async def run(args: argparse.Namespace, pack: augusta_agent.Pack) -> int:
    """Plays every Match in turn; returns the process's exit code."""
    try:
        async with asyncio.timeout(args.timeout):
            for _ in range(args.matches):
                await play_match(args, pack)
    except TimeoutError:
        print(f"timed out after {args.timeout} s", file=sys.stderr)
        return 1
    except augusta_agent.AgentFailed as failed:
        print(failed, file=sys.stderr)
        return 1
    return 0


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    """The run's options, from argv or, if None, the command line."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", default="127.0.0.1:7777")
    parser.add_argument("--pack", required=True, help="the client pack")
    parser.add_argument(
        "--public-key", required=True, help="the key the pack is signed with"
    )
    parser.add_argument("--character", default="soldier")
    parser.add_argument(
        "--matches",
        type=int,
        default=1,
        help="the Matches to play, one Agent each (default 1)",
    )
    parser.add_argument(
        "--seconds",
        type=float,
        default=10.0,
        help="seconds each Agent plays its Match (default 10)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=120.0,
        help="seconds the run may take (default 120)",
    )
    args = parser.parse_args(argv)
    if args.matches < 1:
        parser.error("--matches must be 1 or more")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    """Plays the Matches; returns the process's exit code."""
    args = parse_args(argv)
    try:
        pack = augusta_agent.load_pack(args.pack, args.public_key)
    except augusta_agent.PackError as error:
        print(error, file=sys.stderr)
        return 1
    return asyncio.run(run(args, pack))


if __name__ == "__main__":
    sys.exit(main())

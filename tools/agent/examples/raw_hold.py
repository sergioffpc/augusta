"""The smallest Agent: one, holding one Raw Command, until interrupted.

Connects an Agent to a server, holds a Command that walks it forward, and
prints where the server has it every second until Ctrl+C or a failure.

Usage (from tools/, after `uv sync` and a build with AUGUSTA_TOOLS):
    uv run python agent/examples/raw_hold.py --server 127.0.0.1:27015 \
        --pack <client.pack> --public-key <pack.pub>
"""

import argparse
import sys
import time

import augusta_agent


def main() -> int:
    """Runs the example; returns the process's exit code."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", default="127.0.0.1:27015")
    parser.add_argument("--pack", required=True, help="the client pack")
    parser.add_argument(
        "--public-key", required=True, help="the key the pack is signed with"
    )
    parser.add_argument("--character", default="soldier")
    args = parser.parse_args()

    try:
        pack = augusta_agent.load_pack(args.pack, args.public_key)
    except augusta_agent.PackError as error:
        print(error, file=sys.stderr)
        return 1

    with augusta_agent.Agent(args.server, pack, args.character) as agent:
        agent.set_raw(augusta_agent.Command(move=(0.0, 0.0, -1.0)))
        try:
            while agent.failure is None:
                view = agent.view()
                print(f"{view.phase.name}: at {view.own.position}")
                time.sleep(1.0)
        except KeyboardInterrupt:
            return 0
        print(agent.failure, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

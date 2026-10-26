# agent

`augusta_agent`. Client-only: the headless server never links it.

## Responsibility

The Agent's Runtime: what runs an Agent live, a player a script controls through
Intents. Each Agent is one Harness Session under a Runner of its own, with no
window or GPU, its Commands made from the Intents it was set. It reports the
events it is told of, how each Intent ended and its failure to one queue that
nothing on the Runner's threads waits on. It speaks to the server through the
Harness alone, so it links no protocol or networking of its own.

## Not its responsibility

- The Python bindings and the dispatch thread that hands Reports to a script:
  the Agents' Python module (`augusta_agent_py`: `module.cpp`, `bindings.h`,
  `dispatch.h`).
- The in-process server the tests play against: `augusta_agent_testing_server`,
  beside it.
- The Session, its prediction and its Intents' execution: harness.
- The connection and its conversions: link.

## Decisions

[ADR-0052](../../../docs/adr/0052-python-scripted-agents.md),
[ADR-0060](../../../docs/adr/0060-executables-are-glue.md).

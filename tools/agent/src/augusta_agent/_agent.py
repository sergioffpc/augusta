"""An Agent as a script's asyncio code sees it, over the native one.

The native Agent reports what happens to it on the module's one dispatch
thread (augusta_agent._native); each Report is handed from there to the
asyncio loop the Agent was made on, and only that loop's thread touches an
Agent's futures and events.
"""

import asyncio
import collections
from collections.abc import Callable
import contextlib
import weakref

from augusta_agent import _native

Event = (
    _native.Shot
    | _native.HitConfirmation
    | _native.Death
    | _native.MatchStart
    | _native.MatchEnd
)
MovementIntent = _native.Move | _native.MoveTo
AimIntent = _native.Look | _native.AimAt
TriggerIntent = _native.HoldFire | _native.Reload | _native.FireBursts


# The spec's name (ADR-0052): what a script catches is that the Agent failed.
class AgentFailed(Exception):  # noqa: N818
    """The Agent stopped playing; there is no reconnecting.

    It was refused, could not reach the server or lost its connection, or its
    threads failed. Its message is the sentence augustac would show.
    """


class Agent:
    """A player with no one at the keyboard, connected until closed.

    Made inside a running asyncio loop, whose thread every call of it must
    come from; with closes it on the way out. Every pending await of it raises
    AgentFailed once it fails, and later ones raise it at once; failure says
    why.
    """

    def __init__(
        self,
        server: str,
        pack: _native.Pack,
        character: str,
        *,
        auto_ready: bool = True,
    ):
        """Connects to server as character, predicting against pack's Map.

        With auto_ready, it reports Ready for every Roster it is sent;
        without, only when ready() is called.
        """
        self._loop = asyncio.get_running_loop()
        # Every event until next_event takes it: none is dropped, so a script
        # that drains late still sees each Match start and end.
        self._events: collections.deque[Event] = collections.deque()
        self._event_waiters: collections.deque[asyncio.Future[Event]] = (
            collections.deque()
        )
        self._endings: dict[int, asyncio.Future[_native.IntentEnding]] = {}
        self._failure: str | None = None
        self._native = _native.Agent(
            server,
            pack,
            character,
            auto_ready=auto_ready,
            receiver=_receiver(weakref.ref(self), self._loop),
        )

    def __enter__(self) -> "Agent":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    @property
    def failure(self) -> str | None:
        """Why the Agent stopped playing, or None while it has not."""
        return self._failure

    def set_movement(
        self, intent: MovementIntent
    ) -> asyncio.Future[_native.IntentEnding]:
        """Moves as intent says, every tick; returns how it ends.

        Move holds a direction and ends only REPLACED; MoveTo heads straight
        for its point and ends ARRIVED there, or BLOCKED once it has come no
        nearer for about a second.
        """
        return self._set(lambda: self._native.set_movement(intent))

    def set_aim(
        self, intent: AimIntent
    ) -> asyncio.Future[_native.IntentEnding]:
        """Aims as intent says, every tick; returns how it ends.

        Look holds a view and ends only REPLACED; AimAt follows its target's
        body down sights and ends TARGET_GONE once it is dead or out of the
        Match.
        """
        return self._set(lambda: self._native.set_aim(intent))

    def set_trigger(
        self, intent: TriggerIntent
    ) -> asyncio.Future[_native.IntentEnding]:
        """Works the trigger as intent says; returns how it ends.

        HoldFire holds it and FireBursts fires in Bursts, reloading when the
        magazine is empty: both end only REPLACED. Reload reloads once and
        ends DONE.
        """
        return self._set(lambda: self._native.set_trigger(intent))

    def set_raw(
        self, command: _native.Command
    ) -> asyncio.Future[_native.IntentEnding]:
        """Holds command on every channel, every tick; returns how it ends.

        Its reload is sent on one tick only and its Seen time is the
        Harness's. The Raw Intent ends REPLACED once any channel is set.
        """
        return self._set(lambda: self._native.set_raw(command))

    def clear_movement(self) -> None:
        """Stands still; the movement Intent ends REPLACED."""
        self._native.clear_movement()

    def clear_aim(self) -> None:
        """Holds the last view; the aim Intent ends REPLACED."""
        self._native.clear_aim()

    def clear_trigger(self) -> None:
        """Releases the trigger; the trigger Intent ends REPLACED."""
        self._native.clear_trigger()

    def ready(self) -> None:
        """Reports Ready for the newest Roster; nothing outside the Lobby."""
        self._native.ready()

    def view(self) -> _native.View:
        """What it has been told so far, and its predicted body: a copy."""
        return self._native.view()

    def stats(self) -> _native.Stats:
        """What its prediction and fire have come to, and its link: a copy.

        netcode counts from its first tick; connection is as of its last
        round of network work, None before it first connected.
        """
        return self._native.stats()

    async def next_event(self) -> Event:
        """The next event it was told of, waiting for one if none is.

        A Shot, Hit confirmation, Death, Match start or Match end, in the
        order they came. Raises AgentFailed once every event before the
        failure has been taken.
        """
        if self._events:
            return self._events.popleft()
        if self._failure is not None:
            raise AgentFailed(self._failure)
        waiter = self._loop.create_future()
        self._event_waiters.append(waiter)
        return await waiter

    def close(self) -> None:
        """Disconnects and stops it; its pending awaits are cancelled."""
        self._native.close()
        for pending in [*self._event_waiters, *self._endings.values()]:
            pending.cancel()
        self._event_waiters.clear()
        self._endings.clear()

    def _set(
        self, set_intent: Callable[[], int]
    ) -> asyncio.Future[_native.IntentEnding]:
        """Sets an Intent with set_intent; returns how it ends."""
        ending = self._loop.create_future()
        # Awaiting it is the script's choice: one it lets go of that fails
        # is not an unretrieved exception.
        ending.add_done_callback(_retrieve)
        if self._failure is not None:
            ending.set_exception(AgentFailed(self._failure))
            return ending
        self._endings[set_intent()] = ending
        return ending

    def _receive(self, report) -> None:
        """Takes one Report, on the loop's thread."""
        if isinstance(report, _native.IntentEnded):
            ending = self._endings.pop(report.intent, None)
            if ending is not None and not ending.done():
                ending.set_result(report.ending)
        elif isinstance(report, _native.AgentFailure):
            self._fail(report.description)
        else:
            self._tell(report)

    def _tell(self, event: Event) -> None:
        while self._event_waiters:
            waiter = self._event_waiters.popleft()
            if not waiter.done():
                waiter.set_result(event)
                return
        self._events.append(event)

    def _fail(self, description: str) -> None:
        self._failure = description
        for pending in [*self._event_waiters, *self._endings.values()]:
            if not pending.done():
                pending.set_exception(AgentFailed(description))
        self._event_waiters.clear()
        self._endings.clear()


def _receiver(
    agent: "weakref.ref[Agent]", loop: asyncio.AbstractEventLoop
) -> Callable[[object], None]:
    """What the dispatch thread calls with each of agent's Reports.

    It hands each to loop's thread. It holds agent weakly, so an Agent the
    script lets go of without closing is still destroyed, which closes it.
    """

    def receive(report) -> None:
        held = agent()
        if held is None:
            return
        # A closed loop raises: no one is left to tell.
        with contextlib.suppress(RuntimeError):
            loop.call_soon_threadsafe(held._receive, report)

    return receive


def _retrieve(future: asyncio.Future) -> None:
    if not future.cancelled():
        future.exception()

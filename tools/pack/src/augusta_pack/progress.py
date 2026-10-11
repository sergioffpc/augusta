"""Throttled progress reporting for long-running pipeline steps."""

import sys
import time
from typing import TextIO


class Progress:
    """Reports `label done/total (detail)`, at most every min_interval secs.

    On a console it rewrites a single line; otherwise (pipe, redirected log)
    it prints one plain line per update, so logs stay readable.
    """

    def __init__(
        self,
        label: str,
        total: int,
        stream: TextIO | None = None,
        min_interval: float = 1.0,
    ) -> None:
        self._label = label
        self._total = total
        self._stream = stream if stream is not None else sys.stderr
        self._min_interval = min_interval
        self._interactive = self._stream.isatty()
        self._last_report = 0.0
        self._last_width = 0
        self._last_done = -1

    def update(self, done: int, detail: str = "") -> None:
        """Reports done of total, unless the last report is too recent."""
        now = time.monotonic()
        if done < self._total and now - self._last_report < self._min_interval:
            return
        self._last_report = now
        self._write(done, detail)

    def finish(self) -> None:
        """Reports the total as done, and ends the line on a console."""
        if self._last_done != self._total:
            self._write(self._total, "")
        if self._interactive:
            self._stream.write("\n")
        self._stream.flush()

    def _write(self, done: int, detail: str) -> None:
        self._last_done = done
        percent = 100 * done // self._total if self._total else 100
        line = f"{self._label} {done}/{self._total} ({percent}%)" + (
            f" - {detail}" if detail else ""
        )
        if self._interactive:
            self._stream.write("\r" + line.ljust(self._last_width))
            self._last_width = len(line)
        else:
            self._stream.write(line + "\n")
        self._stream.flush()

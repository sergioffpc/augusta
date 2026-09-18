"""usd-validation-nvidia (ADR-0015) - called in-process through its own
Python API (usd_validation_nvidia.cli_main), the same package the
`nvidia_usd_validate` console script wraps. No subprocess/CLI binary
involved: cli_main() only ever raises SystemExit(1) on failure (see its
own source, usd_validation_nvidia/cli/_validation.py) and returns normally
on success, so catching that here gives exactly the CLI's own pass/fail
signal without spawning a process for it.
"""

from pathlib import Path

import usd_validation_nvidia


class ValidationError(RuntimeError):
    """Raised when nvidia_usd_validate reports issues."""


def validate_stage(stage_path: Path) -> None:
    """Runs usd-validation-nvidia's default rule set against stage_path.

    Exits non-zero on any issue (including warnings), not just failures/
    errors - a clean stage means no output at all.
    """
    try:
        usd_validation_nvidia.cli_main([str(stage_path)])
    except SystemExit as error:
        raise ValidationError(f"usd-validation-nvidia found issues (exit {error.code})") from error

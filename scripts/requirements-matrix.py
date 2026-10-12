#!/usr/bin/env python3
r"""Which tests exercise each requirement, and how they did (ADR-0013).

A test names the requirements it exercises (docs/REQUIREMENTS.md's US-nn and
NFR-nn) on a comment line directly above it, in the comment block that
describes it, if any:

  // Requirements: US-07, US-08
  TEST_F(FireTest, HoldingFireFiresAtTheFireRateUntilTheMagazineIsEmpty) {

--check reads those lines in every C++ test under tests/ and tools/*/tests/
and fails on one that names a requirement the document doesn't have, or that
is not directly above a test. Given ctest's JUnit XML (--junit), it writes
instead an HTML matrix of every requirement, the tests that name it and each
one's results (--output), and prints a one-line summary of it.

  scripts/requirements-matrix.py --check
  scripts/requirements-matrix.py --junit <junit-dir> --output requirements.html
"""

import argparse
import dataclasses
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_REQUIREMENT = re.compile(r"^### ((?:US|NFR)-\d+): (.+)$", re.MULTILINE)
_MARKER = re.compile(r"^\s*//\s*Requirements:(.*)$")
_COMMENT = re.compile(r"^\s*//")
# GoogleTest's and RapidCheck's test macros, whose first two arguments are the
# suite and the test, the name ctest gives it as <suite>.<test>.
_TEST = re.compile(
    r"^(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P|RC_GTEST_PROP"
    r"|RC_GTEST_FIXTURE_PROP)\(\s*(\w+)\s*,\s*(\w+)",
    re.MULTILINE,
)
# How a requirement or a test did, worst first.
_OUTCOMES = ("failed", "passed", "not run", "untested")
_STYLE = """
body { font-family: system-ui, sans-serif; margin: 2rem; }
table { border-collapse: collapse; }
th, td { border: 1px solid #ccc; padding: 0.25rem 0.5rem; text-align: left;
  vertical-align: top; }
ul { margin: 0; padding-left: 1rem; }
.where, .runs { color: #666; font-size: 0.85em; }
.passed { color: #1a7f37; }
.failed { color: #cf222e; }
.not-run, .untested { color: #9a6700; }
"""


@dataclasses.dataclass
class Test:
    """A test that names requirements, where it is and what it names."""

    name: str
    path: pathlib.Path
    line: int
    requirements: list[str]


def _relative(path):
    return path.relative_to(_ROOT).as_posix()


def _css_class(outcome):
    return outcome.replace(" ", "-")


def requirements(document):
    """Each requirement's ID and title, in the document's order."""
    return dict(_REQUIREMENT.findall(document.read_text(encoding="utf-8")))


def sources():
    """Every C++ file the runtime's and the tools' tests are in."""
    return sorted(
        [
            *(_ROOT / "tests").rglob("*.cpp"),
            *(_ROOT / "tools").glob("*/tests/**/*.cpp"),
        ]
    )


def marked_tests(path, errors):
    """The tests in path that name requirements; a stray marker is an error."""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    tests = []
    claimed = set()
    for match in _TEST.finditer(text):
        line = text.count("\n", 0, match.start())
        above = line - 1
        ids = None
        while above >= 0 and _COMMENT.match(lines[above]):
            marker = _MARKER.match(lines[above])
            if marker:
                ids = [part.strip() for part in marker.group(1).split(",")]
                claimed.add(above)
            above -= 1
        if ids is not None:
            name = f"{match.group(1)}.{match.group(2)}"
            tests.append(Test(name, path, line + 1, ids))
    for number, text_line in enumerate(lines):
        if _MARKER.match(text_line) and number not in claimed:
            errors.append(
                f"{_relative(path)}:{number + 1}: not directly above a test"
            )
    return tests


def check(tests, known, errors):
    """Adds an error for each requirement a test names that is not known."""
    for test in tests:
        where = f"{_relative(test.path)}:{test.line}: {test.name}"
        for requirement in test.requirements:
            if requirement not in known:
                errors.append(
                    f"{where} names {requirement!r}, which"
                    " docs/REQUIREMENTS.md does not have"
                )


def results(junit):
    """Each ctest test's outcome by the run (JUnit file) that ran it."""
    by_test = {}
    for report in sorted(pathlib.Path(junit).glob("*.xml")):
        for case in ET.parse(report).getroot().iter("testcase"):
            if case.find("failure") is not None or case.get("status") == "fail":
                outcome = "failed"
            elif case.get("status") == "run":
                outcome = "passed"
            else:
                continue
            by_test.setdefault(case.get("name"), {})[report.stem] = outcome
    return by_test


def runs_of(test, by_test):
    """The test's outcome by run, the worst of a parameterized one's."""
    suite, name = test.name.split(".", 1)
    instance = re.compile(
        rf"^(?:\w+/)?{re.escape(suite)}(?:/\d+)?\.{re.escape(name)}(?:/.*)?$"
    )
    runs = {}
    for ctest_name, outcomes in by_test.items():
        if instance.match(ctest_name):
            for run, outcome in outcomes.items():
                if runs.get(run) != "failed":
                    runs[run] = outcome
    return runs


def _worst(outcomes, otherwise):
    return next((each for each in _OUTCOMES if each in outcomes), otherwise)


def _test_item(test, runs):
    outcome = _worst(set(runs.values()), "not run")
    ran = ", ".join(f"{run}: {each}" for run, each in sorted(runs.items()))
    return (
        f'<li class="{_css_class(outcome)}">{html.escape(test.name)}'
        f' <span class="where">{html.escape(_relative(test.path))}:'
        f"{test.line}</span>"
        f' <span class="runs">{html.escape(ran or "not run")}</span></li>'
    ), outcome


def render(known, tests, by_test):
    """The matrix's HTML page, and the one-line summary of it."""
    rows = []
    counts = dict.fromkeys(_OUTCOMES, 0)
    for requirement, title in known.items():
        named = [test for test in tests if requirement in test.requirements]
        items = [_test_item(test, runs_of(test, by_test)) for test in named]
        outcome = _worst({each for _, each in items}, "untested")
        counts[outcome] += 1
        rows.append(
            f"<tr><td>{requirement}</td><td>{html.escape(title)}</td>"
            f'<td class="{_css_class(outcome)}">{outcome}</td>'
            f"<td>{len(named)}</td>"
            f"<td><ul>{''.join(item for item, _ in items)}</ul></td></tr>"
        )
    summary = (
        f"{len(known)} requirements: {counts['passed']} passed,"
        f" {counts['failed']} failed, {counts['not run']} whose tests did not"
        f" run, {counts['untested']} with no test"
    )
    header = "".join(
        f"<th>{each}</th>"
        for each in ("Requirement", "Title", "Result", "Tests", "Each test")
    )
    page = "\n".join(
        [
            "<!doctype html>",
            '<html lang="en">',
            "<head>",
            '<meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,'
            ' initial-scale=1">',
            "<title>Requirements and their tests</title>",
            f"<style>{_STYLE}</style>",
            "</head>",
            "<body>",
            "<h1>Requirements and their tests</h1>",
            f"<p>{html.escape(summary)}. A requirement fails when any test"
            " that names it failed, and passes when the others that ran all"
            " passed (docs/REQUIREMENTS.md, ADR-0013).</p>",
            f"<table><tr>{header}</tr>",
            *rows,
            "</table>",
            "</body>",
            "</html>",
            "",
        ]
    )
    return page, summary


def main():
    """Checks the markers, or renders the matrix."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--junit", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not args.check and not (args.junit and args.output):
        parser.error("either --check, or --junit and --output")

    known = requirements(_ROOT / "docs" / "REQUIREMENTS.md")
    errors = []
    tests = [test for path in sources() for test in marked_tests(path, errors)]
    check(tests, known, errors)
    if args.check:
        for error in errors:
            print(error, file=sys.stderr)
        return 1 if errors else 0

    page, summary = render(known, tests, results(args.junit))
    args.output.write_text(page, encoding="utf-8")
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())

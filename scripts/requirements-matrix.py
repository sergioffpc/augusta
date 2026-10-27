#!/usr/bin/env python3
r"""Which tests and CI jobs check each requirement and how tests did (ADR-0013).

A test names the requirements it exercises (docs/REQUIREMENTS.md's US-nn and
NFR-nn) on a comment line directly above it, in the comment block that
describes it, if any:

  // Requirements: US-07, US-08
  TEST_F(FireTest, HoldingFireFiresAtTheFireRateUntilTheMagazineIsEmpty) {

A CI job that checks a requirement no test can (a build, a render, a policy
fixture) names it the same way, directly above its key in a GitHub workflow:

  # Requirements: NFR-11, NFR-15
  manifests:

--check reads those lines in every C++ test under tests/ and tools/*/tests/
and in every workflow under .github/workflows/, and fails on one that names a
requirement the document doesn't have, or that is not directly above a test or
a job. Given ctest's JUnit XML (--junit), it writes instead an HTML matrix of
every requirement, the tests and CI jobs that name it and each test's results
(--output), and prints a one-line summary of it. A job's outcome is not read:
the matrix shows it as checked in CI.

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
_YAML_MARKER = re.compile(r"^\s*#\s*Requirements:(.*)$")
_YAML_COMMENT = re.compile(r"^\s*#")
# A workflow's top-level jobs key, and a key in a YAML mapping.
_JOBS = re.compile(r"^jobs:\s*(?:#.*)?$")
_KEY = re.compile(r"^( *)([\w-]+):\s*(?:#.*)?$")
# GoogleTest's and RapidCheck's test macros, whose first two arguments are the
# suite and the test, the name ctest gives it as <suite>.<test>.
_TEST = re.compile(
    r"^(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P|RC_GTEST_PROP"
    r"|RC_GTEST_FIXTURE_PROP)\(\s*(\w+)\s*,\s*(\w+)",
    re.MULTILINE,
)
# How a requirement, a test or a CI job did, worst first: a requirement takes
# its tests' outcome when it has any, and is checked in CI when only jobs name
# it.
_OUTCOMES = ("failed", "passed", "not run", "checked in CI", "untested")
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
.checked-in-ci { color: #0969da; }
"""


@dataclasses.dataclass
class Test:
    """A test that names requirements, where it is and what it names."""

    name: str
    path: pathlib.Path
    line: int
    requirements: list[str]

    @property
    def label(self):
        """How the test is named in errors."""
        return self.name


@dataclasses.dataclass
class Job:
    """A CI job that names requirements, where its key is and what it names."""

    name: str
    path: pathlib.Path
    line: int
    requirements: list[str]

    @property
    def label(self):
        """How the job is named in errors."""
        return f"CI job {self.name}"


def _relative(path):
    return path.relative_to(_ROOT).as_posix()


def _css_class(outcome):
    return outcome.replace(" ", "-").lower()


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


def workflows():
    """Every GitHub workflow, where the CI jobs are."""
    return sorted((_ROOT / ".github" / "workflows").glob("*.y*ml"))


def _ids_above(lines, line, comment, marker, claimed):
    """The IDs a marker names in the comment block directly above line.

    None when the block has no marker; a marker's line is added to claimed.
    """
    above = line - 1
    ids = None
    while above >= 0 and comment.match(lines[above]):
        match = marker.match(lines[above])
        if match:
            ids = [part.strip() for part in match.group(1).split(",")]
            claimed.add(above)
        above -= 1
    return ids


def _strays(path, lines, marker, claimed, what, errors):
    """Adds an error for each marker in lines that nothing claimed."""
    for number, text_line in enumerate(lines):
        if marker.match(text_line) and number not in claimed:
            errors.append(
                f"{_relative(path)}:{number + 1}: not directly above {what}"
            )


def marked_tests(path, errors):
    """The tests in path that name requirements; a stray marker is an error."""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    tests = []
    claimed = set()
    for match in _TEST.finditer(text):
        line = text.count("\n", 0, match.start())
        ids = _ids_above(lines, line, _COMMENT, _MARKER, claimed)
        if ids is not None:
            name = f"{match.group(1)}.{match.group(2)}"
            tests.append(Test(name, path, line + 1, ids))
    _strays(path, lines, _MARKER, claimed, "a test", errors)
    return tests


def job_keys(lines):
    """Each job's key line in a workflow's lines, and the job's name.

    A job key is a key of the top-level jobs mapping, indented as its first.
    """
    keys = []
    indent = None
    in_jobs = False
    for number, text_line in enumerate(lines):
        if not text_line.strip() or _YAML_COMMENT.match(text_line):
            continue
        if not text_line.startswith(" "):
            in_jobs = bool(_JOBS.match(text_line))
            continue
        key = _KEY.match(text_line)
        if in_jobs and key:
            indent = key.group(1) if indent is None else indent
            if key.group(1) == indent:
                keys.append((number, key.group(2)))
    return keys


def marked_jobs(path, errors):
    """The CI jobs in path naming requirements; a stray marker is an error."""
    lines = path.read_text(encoding="utf-8").splitlines()
    jobs = []
    claimed = set()
    for line, name in job_keys(lines):
        ids = _ids_above(lines, line, _YAML_COMMENT, _YAML_MARKER, claimed)
        if ids is not None:
            jobs.append(Job(name, path, line + 1, ids))
    _strays(path, lines, _YAML_MARKER, claimed, "a job", errors)
    return jobs


def check(named, known, errors):
    """Adds an error for each unknown requirement a test or a job names."""
    for each in named:
        where = f"{_relative(each.path)}:{each.line}: {each.label}"
        for requirement in each.requirements:
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


def _job_item(job):
    outcome = "checked in CI"
    return (
        f'<li class="{_css_class(outcome)}">CI job'
        f" <code>{html.escape(job.name)}</code>"
        f' <span class="where">{html.escape(_relative(job.path))}:'
        f"{job.line}</span>"
        f' <span class="runs">{outcome}</span></li>'
    ), outcome


def render(known, tests, jobs, by_test):
    """The matrix's HTML page, and the one-line summary of it."""
    rows = []
    counts = dict.fromkeys(_OUTCOMES, 0)
    for requirement, title in known.items():
        named = [test for test in tests if requirement in test.requirements]
        items = [_test_item(test, runs_of(test, by_test)) for test in named]
        items += [
            _job_item(job) for job in jobs if requirement in job.requirements
        ]
        outcome = _worst({each for _, each in items}, "untested")
        counts[outcome] += 1
        rows.append(
            f"<tr><td>{requirement}</td><td>{html.escape(title)}</td>"
            f'<td class="{_css_class(outcome)}">{outcome}</td>'
            f"<td>{len(items)}</td>"
            f"<td><ul>{''.join(item for item, _ in items)}</ul></td></tr>"
        )
    summary = (
        f"{len(known)} requirements: {counts['passed']} passed,"
        f" {counts['failed']} failed, {counts['not run']} whose tests did not"
        f" run, {counts['checked in CI']} checked only in CI,"
        f" {counts['untested']} named by no test or CI job"
    )
    header = "".join(
        f"<th>{each}</th>"
        for each in ("Requirement", "Title", "Result", "Checks", "Each check")
    )
    page = "\n".join(
        [
            "<!doctype html>",
            '<html lang="en">',
            "<head>",
            '<meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,'
            ' initial-scale=1">',
            "<title>Requirements and their checks</title>",
            f"<style>{_STYLE}</style>",
            "</head>",
            "<body>",
            "<h1>Requirements and their checks</h1>",
            f"<p>{html.escape(summary)}. A requirement fails when any test"
            " that names it failed, and passes when the others that ran all"
            " passed. One that only CI jobs name is checked in CI: the matrix"
            " does not read the jobs' results (docs/REQUIREMENTS.md,"
            " ADR-0013).</p>",
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
    jobs = [job for path in workflows() for job in marked_jobs(path, errors)]
    check([*tests, *jobs], known, errors)
    if args.check:
        for error in errors:
            print(error, file=sys.stderr)
        return 1 if errors else 0

    page, summary = render(known, tests, jobs, results(args.junit))
    args.output.write_text(page, encoding="utf-8")
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""MkDocs build hooks for the documentation site (ADR-0046).

The site reads the repository's Markdown as it is. These hooks make that work:
README.md and CONTEXT.md, which live at the root, join docs/ as pages; the
navigation lists the ADRs in number order, so a new one needs no edit to
mkdocs.yml; and a link to anything that is not a page becomes a link to it on
GitHub, so the same Markdown reads right in both places.
"""

import posixpath
import re
from pathlib import Path

from mkdocs.structure.files import File

REPO_ROOT = Path(__file__).resolve().parents[2]
GITHUB = "https://github.com/sergioffpc/augusta"

# Root documents published as pages, by their path in the repository.
ROOT_PAGES = {"README.md": "index.md", "CONTEXT.md": "CONTEXT.md"}

# The navigation before the ADRs, in reading order: (title, page).
LEADING_NAV = [
    ("Home", "index.md"),
    ("Vision", "VISION.md"),
    ("Requirements", "REQUIREMENTS.md"),
    ("Architecture", "ARCHITECTURE.md"),
    ("Context", "CONTEXT.md"),
    ("Engineering", "ENGINEERING.md"),
    ("Roadmap", "ROADMAP.md"),
]

LINK = re.compile(r"(\]\()([^)\s]+)(\))")
ADR = re.compile(r"^(\d{4})-.*\.md$")


def _title(path: Path) -> str:
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return path.stem


def on_config(config):
    adrs = []
    for path in sorted((REPO_ROOT / "docs" / "adr").glob("*.md")):
        match = ADR.match(path.name)
        if match:
            adrs.append({f"{match.group(1)} · {_title(path)}": f"adr/{path.name}"})
    config["nav"] = [{title: page} for title, page in LEADING_NAV] + [
        {"Decisions (ADRs)": adrs},
        {"API reference": "api/"},
        {"Benchmarks": "benchmarks/"},
    ]
    return config


def on_files(files, config):
    for repo_path, src_uri in ROOT_PAGES.items():
        files.append(File.generated(config, src_uri, abs_src_path=str(REPO_ROOT / repo_path)))
    return files


def _repo_path(src_uri: str) -> str:
    """Where the page at src_uri lives in the repository."""
    for repo_path, uri in ROOT_PAGES.items():
        if uri == src_uri:
            return repo_path
    return f"docs/{src_uri}"


def _page_uri(repo_path: str):
    """The page a repository path is published as, or None if it is not one."""
    if repo_path in ROOT_PAGES:
        return ROOT_PAGES[repo_path]
    if repo_path.startswith("docs/") and repo_path.endswith(".md") and not repo_path.startswith("docs/agents/"):
        return repo_path[len("docs/"):]
    return None


def on_page_markdown(markdown, page, config, files):
    src_uri = page.file.src_uri
    page_dir = posixpath.dirname(_repo_path(src_uri))

    def rewrite(match):
        target = match.group(2)
        if re.match(r"^[a-z]+:", target) or target.startswith(("#", "/")):
            return match.group(0)
        path, _, anchor = target.partition("#")
        repo_path = posixpath.normpath(posixpath.join(page_dir, path))
        uri = _page_uri(repo_path)
        if uri is not None:
            new = posixpath.relpath(uri, posixpath.dirname(src_uri) or ".")
        else:
            kind = "tree" if (REPO_ROOT / repo_path).is_dir() else "blob"
            new = f"{GITHUB}/{kind}/main/{repo_path}"
        if anchor:
            new = f"{new}#{anchor}"
        return f"{match.group(1)}{new}{match.group(3)}"

    return LINK.sub(rewrite, markdown)

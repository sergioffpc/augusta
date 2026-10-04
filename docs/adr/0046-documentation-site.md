# Documentation Site: MkDocs Material for the Docs, Doxygen for the C++ API, on GitHub Pages

The project's documentation lives in two forms: Markdown written for people
(`README.md`, `CONTEXT.md`, `docs/` and its ADRs) and the doc comments of the C++
headers (`docs/agents/coding-standards.md`). Both are published together as one
site on the repository's GitHub Pages, built from the repository on every push to
`main`, so what the site says is what was released (ADR-0026).

**MkDocs Material builds the site from the Markdown as it is.** No document is
converted or moved for it: `mkdocs.yml` reads `docs/`, and a build hook
(`tools/docs/hooks.py`) adds the two root documents, `README.md` as the home page
and `CONTEXT.md`, lists the ADRs in number order under their own titles, and turns a
link to anything that is not a page (a source file, the `Makefile`, `LICENSE`) into
a link to that file on GitHub, so the same Markdown reads right on GitHub and on the
site. `docs/agents/` is left out: it instructs coding agents, not readers.

**Doxygen builds the C++ API reference** from every header under `src/`
(`tools/docs/Doxyfile`), into the site's `api/` folder, linked from the site's
navigation. It reads the comments the headers already carry: a header's module
comment is its file description (`/// \file`), and a public symbol's `///` comment
is its own. Every public symbol is listed, commented or not, since a symbol whose
name says everything has no comment by rule.

**GitHub Actions publishes it** (`.github/workflows/docs.yml`): every pull request
builds the site, so a broken build fails before merge, and a push to `main` builds
and deploys it with GitHub's own Pages actions. `make docs` builds the same site
locally, into `build/docs-site/`. MkDocs runs through `uv tool run`, pinned, as the
other Python tools do; Doxygen is installed on the machine, like clang-format.

## Considered Options

- **Doxygen alone**, with the Markdown as its related pages: rejected - one tool,
  but its navigation and search suit an API, not forty-odd ADRs read in order.
- **Sphinx with Breathe and Exhale**, the API in the same theme as the prose:
  rejected - it needs the Markdown rewritten for Sphinx (reStructuredText or MyST's
  dialect) and a Doxygen-to-XML-to-Sphinx pipeline, for a single look the project
  does not need.
- **GitHub's wiki**: rejected - kept apart from the repository, so it neither
  versions with the code nor builds from it.
- **Publishing from `develop`**: rejected - the site would describe what has not
  been released.

## Consequences

- A header's module comment starts with `/// \file`, so Doxygen takes it as the
  file's description; `coding-standards.md` says so.
- Markdown links stay relative and repository-rooted; nothing is written for the
  site alone.
- A new ADR appears on the site without touching `mkdocs.yml`.

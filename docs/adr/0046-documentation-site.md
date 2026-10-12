# Documentation Site: MkDocs Material for the Docs, Doxygen for the C++ API, on GitHub Pages

The project's documentation lives in two forms: Markdown written for people
(`README.md`, `CONTEXT.md`, `docs/` and its ADRs) and the doc comments of the
C++ headers (`docs/agents/coding-standards.md`). Both are published together as
one site on the repository's GitHub Pages, built from the repository on every
push to `main`, so what the site says is what was released (ADR-0026).

**MkDocs Material builds the site from the Markdown as it is.** No document is
converted or moved for it: `mkdocs.yml` reads `docs/`, and a build hook
(`tools/docs/hooks.py`) adds the two root documents, `README.md` as the home
page and `CONTEXT.md`, lists the runbooks (`docs/runbooks/`) and the ADRs, each
under its own title and the ADRs in number order, and turns a link to anything
that is not a page (a source file, the `Makefile`, `LICENSE`) into a link to
that file on GitHub, so the same Markdown reads right on GitHub and on the site.
`docs/agents/` is left out: it instructs coding agents, not readers.

**Doxygen builds the C++ API reference** from every header under `src/`
(`tools/docs/Doxyfile`), into the site's `api/` folder, linked from the site's
navigation. It reads the comments the headers already carry: a header's module
comment is its file description (`/// \file`), and a public symbol's `///`
comment is its own. Every public symbol is listed, commented or not, since a
symbol whose name says everything has no comment by rule.

**GitHub Actions publishes it** (`.github/workflows/docs.yml`): every pull
request builds the site, so a broken build fails before merge, and a push to
`main` builds and deploys it with GitHub's own Pages actions. `make docs` builds
the same site locally, into `build/docs-site/`. MkDocs is pinned in the `docs`
group of the Python environment the tools share (`tools/pyproject.toml`) and
runs from it; Doxygen is installed on the machine, like clang-format.

**The site also charts the nightly benchmarks' history** (ADR-0013), under
`benchmarks/`, linked from the navigation: the page and results the nightly
keeps on the `benchmarks` branch, copied in when the site is built for
deployment. **Beside it, under `tests/`, is the nightly's test report**
(ADR-0013), its tests' results and coverage, the latest scheduled night's,
downloaded from that run's artifacts when the site is built for deployment. So
that both show the latest night, `main`'s site is also deployed once a day,
after the nightly; what it says of the code is still what was released.

## Considered Options

- **Doxygen alone**, with the Markdown as its related pages: rejected - one
  tool, but its navigation and search suit an API, not forty-odd ADRs read in
  order.
- **Sphinx with Breathe and Exhale**, the API in the same theme as the prose:
  rejected - it needs the Markdown rewritten for Sphinx (reStructuredText or
  MyST's dialect) and a Doxygen-to-XML-to-Sphinx pipeline, for a single look the
  project does not need.
- **GitHub's wiki**: rejected - kept apart from the repository, so it neither
  versions with the code nor builds from it.
- **Publishing from `develop`**: rejected - the site would describe what has not
  been released.

## Consequences

- A header's module comment starts with `/// \file`, so Doxygen takes it as the
  file's description; `coding-standards.md` says so.
- Markdown links stay relative and repository-rooted; nothing is written for the
  site alone.
- A new ADR or runbook appears on the site without touching `mkdocs.yml`.

# Coding Style

C++ code follows the Google C++ Style Guide, enforced via `clang-format` and
`clang-tidy` in CI.

Markdown follows the
[Google Markdown style guide](https://google.github.io/styleguide/docguide/style.html).
Prettier enforces its layout (paragraphs wrapped at 80 columns, ATX headings,
4-space nesting in lists, fenced code blocks) and pymarkdown lints the rest it
can check (line length, heading style, fenced blocks with a declared language,
no trailing whitespace, no inline HTML). What no tool checks is written by hand
to the guide: one H1 as the document's title, unique and descriptive headings,
lazy `1.` numbering for long lists, informative link text, reference links for
long or repeated links, lists rather than tables where the data reads as a list,
and a trailing backslash, not trailing spaces, for a hard line break.

Python follows the
[Google Python Style Guide](https://google.github.io/styleguide/pyguide.html).
ruff formats it (80 columns, 4-space indentation) and lints it with the guide's
checks as ruff has them: pylint's, which the guide lints with, naming,
Google-convention docstrings (a one-line summary, then `Args:`, `Returns:` and
`Raises:` sections), one import per line sorted within its group, no relative
imports, no catch-all `except`, no shadowed builtins, no unused arguments
(deleted with `del` when a signature is fixed) and logging with %-style
arguments. Tests need no docstrings, nor do dunder methods: `__init__`'s
arguments are the class docstring's. What no tool checks is written by hand: a
docstring for every public function that is not short and obvious,
`"""See base class."""` (or the overridden method's name) on an override, and
the guide's TODO and comment style.

Shell scripts and the git hooks follow the
[Google Shell Style Guide](https://google.github.io/styleguide/shellguide.html),
Bash only (`#!/bin/bash`). shfmt formats them (2-space indentation, `case`
patterns indented, a continued `|`, `&&` or `||` starting the next line) and
shellcheck lints them with the guide's optional checks on (`.shellcheckrc`):
`[[ ... ]]` rather than `[ ... ]`, `"${var}"` rather than `"$var"`, every
expansion quoted, and `-n`/`-z` rather than a bare test. What no tool checks is
written by hand: 80 columns, a file comment saying what the script does, a
comment on every function that is not obvious and short (with its arguments,
globals, outputs and return), `local` for a function's variables, `readonly`
constants in upper case, and a `main` function, called last as `main "$@"`, in
any script with another function.

## Summary

<!-- The smallest view that makes the change clear: pseudocode, a call tree, a
file tree, a Mermaid diagram or a diff sketch. Keep prose brief and use the
domain words in GLOSSARY.md. -->

## Evidence

<!-- Before/after proof that it works: a failing then passing test run, output,
or a screenshot. -->

- **Before:**
- **After:**

## Merge Danger

<!-- Door: one-way (hard to undo once merged, e.g. a wire format, a migration,
a published artifact) or two-way (a revert undoes it). Blast radius: one word
for what breaks if it is wrong. -->

**Door:**

**Blast Radius:**

## Checklist

- [ ] Builds and tests pass locally on the platform(s) this touches
- [ ] Commit messages follow Conventional Commits
- [ ] `docs/` updated if this changes architecture, engineering conventions, or
      the roadmap (see `docs/agents/domain.md`)
- [ ] Linked to the relevant issue, if any, and closed once merged into
      `develop` (see `docs/agents/issue-tracker.md`)

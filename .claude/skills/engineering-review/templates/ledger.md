# Ledger template

One ledger per repository, `ledger.md` at the location the project profile
names. It is the only place finding identity lives across reviews; reports link
to it. Statuses and their rules are in [../runbook.md](../runbook.md), Phase 8.

Each review updates the ledger in place: it adds rows for new findings and adds
one history entry per row it re-checked. History is appended, never rewritten.

```markdown
# Engineering review ledger

Next ID: ER-<n>

## Findings

| ID | Fingerprint | Category | Severity | Status | First seen | Last checked |
| -- | ----------- | -------- | -------- | ------ | ---------- | ------------ |

## History

### ER-<n>

Former fingerprints: <old fingerprint, when the anchor was renamed; or none>

- <YYYY-MM-DD> <commit short hash> <report file>: <status>. <one line of
  evidence: what was re-checked and how>.
```

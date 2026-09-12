# Run history

Menu **Processes → Run History**. Lists the latest runs (Shell and HTTP)
with:

- **Timestamp** and **duration**
- **Status** (✓ success / ✗ failure)
- **Type** (shell/http) and command name
- **Saved output** (stdout/stderr, truncated)

## Actions

- Select a run → see the full output.
- **Re-run** → fires the command again.
- **Clear** → wipes the history.

## Persistence

`~/.config/kai/runs.json` — keeps up to 200 records (most recent first);
each run's output is capped so it never grows unbounded.

# Kai KIP demos

Small programs that speak **KIP** (Kai Interface Protocol, see `docs/kip.md`).
Each one is a command in `kai.yml`; run them from the Kai tree and the output
panel turns into a native screen (forms, confirmations, progress, checklists,
tables).

Most scripts here are **plain bash** (1b and 13–16 use the helpers Kai injects, see their rows): the protocol is JSON Lines, so each script just prints
JSON on stdout and reads Kai's answers (one JSON line) from stdin. There is no
helper, no `jq` and no dependency on the `kai` binary — only `bash`, `sed`,
`printf`, `date` and friends. Every script starts with the same four tiny
functions (`send`, `esc`, `jget`, `recv`) so it can be copied on its own.

| # | Script | What it shows |
|---|---|---|
| 1 | `deploy.sh` | select + flags, danger confirm (pick **Production**, then Back), progress, done with *Open URL* / *Copy* |
| 1b | *(inline shell)* | the same wizard written with the **injected `kip` function**: a native command with KIP on gets `kip` (same verbs as `kai kip`, no `kai` binary, so it also runs in WSL and containers) — `kip prompt`, `kip recv`, `kip get`, `kip done`... |
| 2 | `db-restore.sh` | table field, destructive confirm, steps checklist |
| 3 | `k8s-navigator.sh` | cascading `watch`/`patch` fields (context → namespace → pod) |
| 4 | `scaffold.sh` | text / list / folder / flags, `invalid` round trip (try `My App`), *Reveal* action |
| 5 | `onboarding.sh` | live checklist, `notify`, warning result; `kip_window: true` (own window) |
| 6 | `login.sh` | secret field, `set_env` of the declared `KAI_DEMO_TOKEN` (password: `kai`) |
| 7 | *(inline)* | plain command that prints `{{KAI_DEMO_TOKEN}}` — run it after #6 |
| 8 | `convert.sh` | file picker, number, progress, reveal output |
| 9 | `lookup.sh` | query → result table → copy |
| 10 | `raw-protocol.sh` | the smallest possible program, no functions |
| 11 | `not-kip.sh` | KIP command whose program doesn't speak KIP (the *Unsupported* screen) |
| 12 | `branches.sh` | quiver-style picker: paged + filterable list and **chips** (context / sync / PR with confirmation / delete with confirmation) whose result shows in a box without leaving the step |
| 13 | *(inline Python)* | the same wizard written **directly in the command** (language Python): `import kip` — Kai injects the module, no file and no JSON by hand |
| 14 | *(inline Node)* | the same in Node: the global `kip`, promises and top-level `await` |
| 15 | `table-browser.py` | a **DynamoDB-style table browser**: pick a table, server-side pages with a cursor, a `watch`ed filter, and New / Edit / Duplicate / Delete (inline confirm) — fake in-memory data; swap `FakeBackend` for boto3 to use a real table |
| 16 | *(inline shell)* | the same wizard as 13 in the shell: `kip prompt`, `kip recv`, `kip get "$KIP_MSG"`, `kip done` |
| 17 | *(inline PHP)* | the same wizard in PHP: the class `Kip` (`Kip::prompt`, `Kip::progress`, `Kip::done`) — injected, no `require` |
| 18 | `external-tool.py` | a plain Python **file** called by a native `kip: true` command: Kai exports `PYTHONPATH`, so `import kip` works inside it (same for `NODE_PATH` / `PHP_INI_SCAN_DIR`) |

Three protocol details worth knowing when reading the scripts:
- `invalid` keeps the **same** prompt open with the errors shown, so the program
  just waits for the next answer (sending the prompt again would replace the
  screen and lose the errors).
- Every `change` of a `watch` field must be answered with a `patch` echoing its
  `seq` (see `k8s-navigator.sh`).
- A `patch` without `seq` is *spontaneous* (not an answer to a change) and is always applied: that is how a chip
  repaints a table (`kip.patch(...)` in Python/Node; see `table-browser.py`).
- A `chip` click does **not** end the step: answer with `chip_result` messages
  (`running` … then `success` or `error`) and keep waiting for the real `response`
  (see `branches.sh`).

`fixtures/` holds raw-JSON programs used by the automated tests (timeouts,
malformed lines, cancel handling…); they are not meant for the tree.

# Kai KIP demos

Small programs that speak **KIP** (Kai Interface Protocol, see `docs/kip.md`).
Each one is a command in `kai.json`; run them from the Kai tree and the output
panel turns into a native screen (forms, confirmations, progress, checklists,
tables).

They are **plain bash**: the protocol is JSON Lines, so each script just prints
JSON on stdout and reads Kai's answers (one JSON line) from stdin. There is no
helper, no `jq` and no dependency on the `kai` binary — only `bash`, `sed`,
`printf`, `date` and friends. Every script starts with the same four tiny
functions (`send`, `esc`, `jget`, `recv`) so it can be copied on its own.

| # | Script | What it shows |
|---|---|---|
| 1 | `deploy.sh` | select + flags, danger confirm (pick **Production**, then Back), progress, done with *Open URL* / *Copy* |
| 1b | `helper/deploy.sh` | the same wizard written with the **`kai kip` helper** (needs the `kai` binary on the script's `PATH`; inside WSL under a Windows Kai it falls back to `kai.exe`, or `KAI_EXE`) |
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

Three protocol details worth knowing when reading the scripts:
- `invalid` keeps the **same** prompt open with the errors shown, so the program
  just waits for the next answer (sending the prompt again would replace the
  screen and lose the errors).
- Every `change` of a `watch` field must be answered with a `patch` echoing its
  `seq` (see `k8s-navigator.sh`).
- A `chip` click does **not** end the step: answer with `chip_result` messages
  (`running` … then `success` or `error`) and keep waiting for the real `response`
  (see `branches.sh`).

`fixtures/` holds raw-JSON programs used by the automated tests (timeouts,
malformed lines, cancel handling…); they are not meant for the tree.

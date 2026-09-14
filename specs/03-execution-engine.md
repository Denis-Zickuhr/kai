# Spec 03: Execution Engine, Processes & Hooks

## 1. Subprocesses via `QProcess`/PTY
- Every Command runs asynchronously (the command type was called "Shell" before; the JSON type is now `command`, and `shell` is still accepted on read).
- Before execution, Kai interpolates variables in precedence order:
  `Global < Folder/Project < Dynamic extractions < Form parameters`.
- Variables are injected into the process environment via
  `QProcessEnvironment`.
- `stdout`/`stderr` are captured live and streamed to the output panel; a
  command with `interactive_terminal` set instead feeds a real terminal
  grid (libvterm) over a PTY (Unix `forkpty`) / ConPTY (Windows).

## 2. Terminal targets and execution via WSL
- A Command can pick a **terminal target** that wraps the final
  command in a template (`command_template`, with a `{{command}}`
  placeholder). Main use case: running commands in **WSL** from Kai running
  natively on Windows.
- **Safe injection:** `{{command}}` gets its single quotes escaped
  (`'\''`); `{{command_b64}}` is **crash-proof** injection — the command is
  base64-encoded (no problematic characters) and the template decodes it
  on the other end (e.g. `bash -lic 'eval "$(echo {{command_b64}} | base64
  -d)"'`).
- **`working_dir` through a terminal target:** applied via a `cd '<dir>' &&
  { <command> }` injected **inside** the shell, with escaped quotes —
  necessary because the launched process's own CWD (`wsl.exe` on the
  Windows side) never crosses into the WSL side, and a login shell resets
  to `$HOME` anyway. Without a terminal target (native Linux), the CWD is
  applied directly via `QProcess::setWorkingDirectory`.

## 2b. Command languages (Native, Python, Node, PHP)
- `Command::language` (`native` | `python` | `node` | `php`, JSON key `language`, omitted when
  native; the old `bash`/`sh`/`pwsh` values load as native) says what the `command` text is. `Command::interpreter` (JSON `interpreter`)
  optionally overrides the global interpreter for that command; it only applies to
  python/node/php.
- **Native** (default): the text is a shell line/script (bash, sh where there is no bash) and `{{VAR}}` is interpolated, as
  always — also with KIP on (a `kip` function is added in POSIX shells, see spec 11 §25; texts too long for the
  command line go to a Kai-owned temp file, §26). **Python/Node/PHP**: the text is **code** and is **never interpolated** — it can
  legitimately contain `{{...}}` (f-strings, dictionaries, JS template strings).
  Variables and parameters reach the program as **environment variables** through the
  same resolved env (`Global < Folder < Dynamic < Parameters`), which also travels
  through terminal targets (exported by the targeted-command prefix).
- `ExecutionPipeline::commandLineFor` is the single place that turns a command into the
  shell line (main command, background start, cleanup hooks and `--dry-run` all go
  through it). For python/node/php, `engine::buildInterpreterCommandLine` builds
  `<interp> -u -c "<bootstrap>"` / `<interp> -e "<bootstrap>"` / `<interp> -r "eval(gzuncompress(base64_decode('…')));"`. The code is zlib-compressed
  + base64 and decoded **by the interpreter itself** (not by `base64`/`$(...)`), so the
  line has no `"`, `$`, backtick, `\` or `!` inside the double quotes — it is the same
  text for POSIX, PowerShell and Cmd, goes through any terminal target template, keeps
  stdin free (KIP) and stays far below the Windows command-line limit.
  - Python runs the code in a fresh globals dict (`__name__ == "__main__"`), `-u` plus
    `PYTHONUNBUFFERED=1` and `PYTHONIOENCODING=utf-8` (defaults: the user's environment
    wins).
  - Node runs it with `vm.runInThisContext` inside an async function (top-level `await`,
    `return`); an uncaught error exits with code 1.
- The interpreter is a **shell line resolved where the command runs** (a WSL target uses
  the WSL's `python3`; a login shell already loads pyenv/nvm). Global values live in
  `settings.json` → `interpreters` (`python`, `node`, `php`; defaults `python3`/`node`/`php`, and
  `python` for the default Python on Windows without a terminal target), edited in
  Settings → Languages. `core::InterpreterSettings::resolve` picks command override,
  then global, then the language default.
- **Injected modules** (resources in `src/engine/kip-modules/`): `kai` always (Python/Node), `kip` on KIP
  commands (the class `Kip` in PHP, which has no `kai`; also written to disk for scripts that run as files, see
  spec 11 §15c). Python: registered in `sys.modules` (`import kai`); Node: globals plus a patched
  `Module._load` (`require('kai')`). `kai` is a thin client of the IPC server (one JSON line per
  request, one per reply; `core::ipcEndpointPath()` is exported as `KAI_IPC_SOCKET`): notify
  (`raise`), show, run, list (`commands`), env-list/env-use, ps, kill, import. If the socket is
  unreachable it falls back to the `kai` CLI for notify/show/run/env.use/kill (WSL script under a
  Windows Kai). Tests: `tests/test_kai_module.cpp` (real `IpcServer`), `tests/test_showcase.cpp`.
- `capture_env` (Export variables) is ignored for python/node/php (no `export` to read back);
  KIP's `set_env` covers the use case. Everything else (background, PTY, hooks,
  conditions, cron, auto-run, KIP) applies unchanged.

## 3. Execution conditions
- A command (main or hook) can carry `executionConditions`: a list of
  `{name, left, op, right}` guards, each side a free-text string run
  through `EnvironmentManager::interpolate` (so `{{VAR}}` and dynamic
  tokens like `{{$timestamp}}` just work). Supported `op` values: `exists`,
  `not_exists`, `eq`, `ne`, `gt`, `ge`, `lt`, `le`, `contains`,
  `not_contains` — numeric comparison when both sides parse as a number,
  string comparison otherwise.
- `conditionCombinator` (`and`/`or`) decides how multiple conditions
  combine. `conditionSkipBehavior` decides what a failed guard means:
  `success` (skip quietly, the pipeline moves on) or `failure` (skip and
  fail the pipeline like any other error).
- Checked once, at the single choke point every command flows through
  (`ExecutionPipeline::runSingleCommand`) — covers the main command,
  pre-hooks and post-hooks alike. Cleanup hooks run detached from that
  path (they must survive the pipeline, even the app, exiting) and carry
  their own equivalent check.
- The skip/failure log message names the condition that decided the
  result (its `name`, or an auto-generated summary if unnamed) —
  essential for debugging when a command has more than one guard.

## 4. Ignoring the exit code
- `Command::ignoreExitCode`: when set, the command is always treated as
  successful, regardless of its exit code. A real process crash
  (signal/segfault) is still reported normally. Exists for tools that
  return non-zero on success by design (`explorer.exe` opening a folder
  from WSL is the canonical example) — applied independently in both the
  single-run and the background-process completion paths, since each
  computes `success` on its own.

## 5. Pre/Post/Cleanup hook pipeline
- **Sequence:** `[Pre-Hooks] -> [Main command] -> [Post-Hooks] -> (always) [Cleanup]`.
- A Pre-Hook returning failure (`exitCode != 0`, HTTP `>= 400`, or an
  unmet execution condition with `failure` behavior) aborts the whole
  chain — the main command never runs.
- **Abort scope:** aborting only stops runners that belong to the *same*
  chain (the main command plus its own pre/post hooks) — never unrelated
  commands running in parallel. A single pipeline-wide "stop everything"
  loop used to reach into unrelated runs (e.g. an interactive terminal
  session running alongside), which is exactly the bug this scoping
  avoids.
- **Hook inheritance by folder hierarchy:** when picking hooks for a
  command, candidates include commands from its own folder **plus every
  ancestor folder**. A utility defined at the project root (e.g.
  "Authenticate") becomes reusable as a hook in every subfolder below it,
  annotated `⤴ <origin folder>` in the picker; the pipeline still resolves
  it by its real id.
- **Cleanup hooks** run detached from the main pipeline (`QProcess`
  started, not tracked as a runner) so they survive the pipeline — and, if
  needed, the app itself — exiting. Cleanup fires exactly once per command
  per run, guarded against duplicate triggers from different termination
  paths (success, failure, manual stop).

## 6. Background process management
- Commands with `is_background: true` keep their `ProcessRunner` alive,
  handed off to a long-lived `ProcessManager`.
- The UI shows a live status badge (Running/Error/Success) and lets you
  stop the process (`SIGTERM` → 2s timeout → `SIGKILL`).
- Every command is started via `setsid` so it becomes the leader of its
  own process group (PGID == its own PID) — `stop()` signals the whole
  group (`kill(-pid, signal)`, negative PGID) rather than just the
  directly-spawned shell, so a `npm run dev &`-style child the script
  spawned doesn't survive as an orphan.

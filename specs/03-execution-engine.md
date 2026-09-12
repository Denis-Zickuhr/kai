# Spec 03: Execution Engine, Processes & Hooks

## 1. Subprocesses via `QProcess`/PTY
- Every Shell command runs asynchronously.
- Before execution, Kai interpolates variables in precedence order:
  `Global < Folder/Project < Dynamic extractions < Form parameters`.
- Variables are injected into the process environment via
  `QProcessEnvironment`.
- `stdout`/`stderr` are captured live and streamed to the output panel; a
  command with `interactive_terminal` set instead feeds a real terminal
  grid (libvterm) over a PTY (Unix `forkpty`) / ConPTY (Windows).

## 2. Terminal targets and execution via WSL
- A Shell command can pick a **terminal target** that wraps the final
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

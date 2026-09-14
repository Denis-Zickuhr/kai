# Spec 11: KIP — Kai Interface Protocol

> **Status:** experimental. Implemented on the `experimental/kip` branch.
> Part A is the contract (protocol + behavior). Part B is the
> implementation plan. Read both before writing code.

# Part A — Contract

## 1. Overview
KIP turns a shell command into a small graphical app rendered with Kai's
own components. Instead of printing text for a terminal, a KIP program
emits **structured messages** ("ask for a folder", "show progress",
"done") and reads the user's answers back from stdin. Kai renders each
step as a native screen — it must **not** look like terminal output.

- A KIP program is always interactive under the hood (request/response
  over stdin/stdout).
- Run in a plain terminal, a KIP program prints raw JSON lines — nearly
  unreadable, and that's accepted: the GUI is the intended consumer.
- Out of scope: answer replay, KIP commands as hooks/background/scheduled
  runs, client-side conditional visibility, options sourced from Kai
  Collections, prompt timeouts.

### Motivating use cases
Every feature below exists for at least one of these; use them as the
acceptance scenarios for the sample apps (§19).
1. **Deploy wizard** — pick environment → confirm (danger) → progress →
   done with "Open URL".
2. **Database restore** — program lists backups in a table field → pick
   one → destructive confirm → steps checklist.
3. **Cascading navigation (k8s/cloud)** — context → namespace → pod,
   each list depending on the previous choice (dynamic fields).
4. **Scaffolding** — name, template list, destination folder, flags →
   done with "Reveal folder".
5. **Team onboarding** — a checklist of install/configure/verify steps
   with live states.
6. **Login** — the script authenticates and exports a token to the
   project's dynamic variables (`set_env`).
7. **File conversion** — filepick → options → progress → reveal output.
8. **Lookup** — query → result table → copy a value.

## 2. Activation
- **`Command::kip`** (`"kip": true` in `kai.json`/`kai.yml`), a checkbox
  "KIP interface" in the command editor. Shell commands only.
- Kai **never rewrites the command string**. The author writes `--kip`
  themselves when the tool needs it (`mytool deploy --kip`), or nothing
  for *native* KIP programs that always speak the protocol. The flag only
  tells Kai to run the command as a KIP session.
- **`Command::kipOpenInWindow`** (`"kip_window": true`): the KIP view opens
  in its own window (§13.4) instead of the docked output panel.
- Kai injects into the process environment (overridable by the user's own
  env like any other var):
  - `KIP_VERSION=1` — protocol major version Kai speaks.
  - `KIP_LOCALE=<pt|en>` — Kai's current UI language, so the program can
    localize its own texts (program texts are not part of Kai's i18n).
- Kai's own params form (`Command::params`) still runs **before** the
  process starts; `{{var}}` interpolation works as usual on the command.
  Kai **never** interpolates `{{...}}` inside protocol messages.

## 3. Transport
- **JSON Lines**: one JSON object per line, UTF-8, `\n`-terminated
  (a trailing `\r` is tolerated and stripped).
  - **stdout** (program → Kai): protocol messages.
  - **stdin** (Kai → program): Kai's messages.
  - **stderr**: free-form log, never parsed.
- Rationale: stdin/stdout is the only channel that survives every
  execution path, including terminal targets (`wsl.exe` bridge on
  Windows).
- **No PTY.** A KIP session always runs in `QProcess` mode
  (`ProcessRunner::setUsePty(false)`), even when the resolved terminal
  target has `usePty=true`.
- Envelope: every message carries `{"kip": 1, "type": "<type>"}`.
- Lines are reassembled across `outputReady` chunks. A line longer than
  **1 MiB** is discarded and logged.
- A stdout line that isn't a JSON object, or is an object without a `kip`
  key, is **not a protocol message**: it goes to the session log (§13.3).
  This tolerates wrappers that print first (e.g. `npm run` echoing
  `> pkg@1.0 deploy`).

## 4. Handshake and "unsupported command" detection
1. The first protocol message must be `hello`:
   ```json
   {"kip":1,"type":"hello","title":"Deploy","version":"2.3.0"}
   ```
   `title` (optional) names the app on screen (fallback: command name);
   `version` (optional) is the program's own version, shown discreetly.
2. **Unsupported** — before a `hello` arrives, the process exits (typical:
   `unknown option --kip`) or the **10 s** handshake timeout expires
   (counted from process start; cold `wsl.exe` starts take seconds). Kai
   stops the process and shows "This command doesn't support KIP", with
   the log expanded. Run result: failure.
3. **ProtocolError** — the first protocol message isn't `hello`, or
   `hello.kip` is greater than the version Kai speaks ("this program
   needs a newer Kai").

## 5. Message catalog

### 5.1 Program → Kai
| `type` | Fields | Effect |
|---|---|---|
| `hello` | `title?`, `version?` | Handshake (§4). |
| `prompt` | `id`, `title?`, `description?`, `submit_label?`, `back?`, `cancellable?`, `remember?`, `fields[]` | Form screen; waits for `response` (§6, §7). |
| `confirm` | `id`, `title?`, `text`, `danger?`, `confirm_label?`, `cancel_label?`, `back?`, `cancellable?` | Yes/no screen; answered with `response` `{"confirmed": bool}` (§8). |
| `patch` | `id`, `seq`, `fields[]`, `remove?[]` | Updates fields of the open prompt `id` (§9). |
| `invalid` | `id`, `errors{field: msg}`, `message?` | Server-side validation failed: keeps prompt `id` open with errors under fields and `message` on top. |
| `message` | `level` (`info`\|`success`\|`warning`\|`error`), `text` | Status card on the current screen. |
| `markdown` | `text` | Rendered Markdown block on the current screen. |
| `progress` | `value` (0–100 or `null` = indeterminate), `label?`, `cancellable?` | Progress bar; a later `progress` updates the same bar. |
| `steps` | `id`, `title?`, `items[]` of `{id, label, state?, detail?}` | Creates/replaces a checklist block (§10). |
| `step` | `steps`, `id`, `state`, `detail?` | Updates one checklist item. |
| `table` | `id?`, `title?`, `columns[]`, `rows[]` | Read-only table block (§11). |
| `notify` | `title`, `text?`, `level?` | OS notification (§12.2). |
| `set_env` | `name`, `value` | Exports a dynamic variable (§12.3). |
| `done` | `title?`, `text?`, `level?` (default `success`), `actions?[]` | Result screen (§12.1). The program should exit right after. |

### 5.2 Kai → program
| `type` | Fields | When |
|---|---|---|
| `response` | `id`, `values{}` | User submitted prompt/confirm `id`. |
| `change` | `id`, `seq`, `field`, `values{}` | A `watch` field changed (§9). |
| `back` | `id` | User clicked Back on prompt/confirm `id` (§7.3). |
| `cancel` | — | User clicked Cancel (§14). |

### 5.3 General rules
- Unknown `type` → ignored, logged as a warning.
- `id`s are program-chosen strings. A `response`/`patch`/`invalid` for an
  id that isn't the open screen is logged and ignored.
- A new `prompt`/`confirm` while another is open replaces it (logged).
- **Screen** = the open prompt/confirm plus every display block
  (`message`, `markdown`, `progress`, `steps`, `table`) emitted since the
  previous screen was answered. Display blocks emitted while no prompt is
  open form a "running" screen. Answering a prompt clears its display
  blocks.
- Limits (exceeding → truncated + logged): 100 fields per prompt, 5000
  options per field, 1000 table rows, 200 checklist items, 64 KiB per
  text value.

## 6. Field types
Common attributes: `name` (required, unique in the prompt), `label`,
`description`, `required` (default `false`), `default`, `placeholder`,
`group`, `watch` (§9), `remember` (§7.2).

| `type` | Extra attributes | Widget | Response value |
|---|---|---|---|
| `text` | — | single-line input | string |
| `secret` | — | masked input with show/hide toggle | string |
| `textarea` | — | auto-growing multi-line input | string |
| `number` | `min?`, `max?`, `step?`, `decimals?` (default 0) | spin box | JSON number |
| `date` | `mode` (`date`\|`time`\|`datetime`, default `date`), `range?` | date picker (reuse `DatePickerDialog`) | ISO string (`yyyy-MM-dd`, `HH:mm:ss`, `yyyy-MM-ddTHH:mm:ss`); with `range`: `{"start","end"}` |
| `select` | `options[]` | dropdown | string |
| `list` | `options[]`, `multiple?` | always-visible list; filter field above when > 8 options | string; with `multiple`: array of strings |
| `table` | `columns[]`, `rows[]`, `row_key?` (default `"id"`), `multiple?` | selectable table; filter field above when > 8 rows | row key string; with `multiple`: array |
| `filepick` | `filter?` (Qt name filter), `initial_dir?`, `path_format?` (`native`\|`posix`\|`windows`) | input + browse button | string |
| `folderpick` | `initial_dir?`, `path_format?` | input + browse button | string |
| `flags` | `options[]` of `{name, label, description?, default: bool}` | group of checkboxes | object `{name: bool}`, every flag present |

- `options[]` (select/list) accepts plain strings or objects
  `{value, label?, description?}`; a string is both value and label.
- `path_format` reuses `utils::convertFilePathFormat`, same semantics as
  the `file` parameter.
- `group`: fields with the same group name go into one collapsible
  section, same behavior as `Parameter::group` (collapsed by default,
  pending-required badge).
- `required` is validated locally (submit disabled, same rule as
  `ParameterFormDialog`); everything else is the program's job via
  `invalid`.
- An unknown field `type` renders as `text` with a warning in the log.
- An unanswered non-required field is sent as its empty value (`""`,
  `[]`, `false` per flag, `null` for number/date).

## 7. Prompt behavior

### 7.1 Submit
Submit button (`submit_label` or the default "Continue"); Enter submits
unless focus is in a textarea/list/table. After submit the form locks
(read-only + spinner) until the next screen, `invalid`, or exit.

### 7.2 Remember last answers
- Default on. Kai pre-fills each field with the last value the user
  submitted for **same command + prompt `id` + field `name`**, taking
  priority over `default` (same rule as `lastParamValues`).
- Opt-out: `remember: false` on the prompt or on a field.
- `secret` fields are **never** remembered.
- A remembered value no longer valid (option gone, out of range) falls
  back to `default`.
- Storage: `Command::kipLastValues` (`kip_last_values`, object keyed
  `"<promptId>/<fieldName>"` → JSON value), persisted and exported exactly
  like `lastParamValues`.

### 7.3 Back
`back: true` on a prompt/confirm shows a Back button. Kai sends
`{"type":"back","id"}`, removes the last entry of the answered-steps
summary, and waits; the program decides which screen comes next.

## 8. Confirm
Two buttons: confirm (`confirm_label`, default "Confirm") and decline
(`cancel_label`, default "Cancel"). `danger: true` styles the confirm
button as destructive (theme error color) and moves default focus to the
decline button, so Enter never confirms a destructive action by accident.
Response: `{"type":"response","id","values":{"confirmed":true|false}}` —
declining is an answer, not a session cancel.

## 9. Dynamic fields (`watch` + `patch`)
- A field with `watch: true` triggers a `change` whenever its value
  changes (text-like fields debounced 300 ms):
  `{"type":"change","id":"p1","seq":7,"field":"context","values":{...all current values...}}`.
- The program **must** answer every `change` with a `patch` echoing its
  `seq` (possibly with empty `fields`):
  `{"type":"patch","id":"p1","seq":7,"fields":[{...full field def...}],"remove":["pod"]}`.
- Each entry in `fields` replaces the field with the same `name` (or is
  appended at the end if new); `remove` drops fields by name.
- While a change is pending, submit is disabled and a subtle loading
  indicator shows on the form. A patch with `seq` lower than the latest
  sent is ignored. A patch **without `seq`** is *spontaneous* (not an answer
  to a change — e.g. a chip repainting a table): it is always applied and it
  does not end a pending change. No patch within **10 s** → submit
  re-enabled, warning logged.
- A replaced field keeps the user's current value when still valid
  (option still exists, within range); otherwise it resets to `default`.

## 10. Steps checklist
`steps` creates a checklist block; `step` updates one item. States:
`pending` (default), `running` (spinner), `success`, `error`, `skipped`;
`detail` is a muted line under the label. A `step` for an unknown
list/item is logged and ignored.

## 11. Tables
`columns[]` of `{key, label}`; `rows[]` of objects keyed by column `key`
(extra keys ignored, missing keys render empty). Cell values are shown as
text. Read-only table blocks support selecting and copying a cell
(context menu + Ctrl+C). The `table` field (§6) uses the same widget with
row selection.

## 12. Kai integrations

### 12.1 Done screen and actions
`done` shows a result card (icon by `level`, `title`, `text`) plus
buttons from `actions[]`:
| action `type` | Fields | Behavior |
|---|---|---|
| `open_url` | `label`, `url` | Opens in the browser. **Only `http`/`https`**; anything else is rejected and logged. |
| `reveal` | `label`, `path`, `path_format?` | Opens the file manager at the path (file → its folder). Path converted to native with `utils::convertFilePathFormat`; a missing path disables the button with a tooltip. |
| `copy` | `label`, `value` | Copies to the clipboard, brief "Copied" feedback. |

"Run again" is always present (re-runs the command through the normal
flow, Kai params form included). Unknown action types are skipped.

### 12.2 Notifications
`notify` goes through Kai's existing notification path
(`MainWindow::maybeShowNotification` / `notification-gate.h`): it respects
the global notifications switch, is only shown as a toast when the KIP
view isn't visible to the user (window hidden/minimized/unfocused), and is
recorded in the notification history like any other notification.

### 12.3 Exporting variables (`set_env`)
- Allowed only for names listed in **`Command::declaredEnvVars`** — the
  same allowlist "Export variables" uses. Scope (`project`/`global`) and
  `persist` come from the declaration, never from the message.
- Undeclared name → rejected, warning in the log + the protocol
  inspector.
- Applied immediately (not at exit) through the same code path used by
  `ExecutionPipeline::ingestCapturedEnv`, with the dynamic-var scope
  captured at run start (never re-read later).
- `captureEnv` itself stays disabled for KIP commands; the declared vars
  list must be editable in the editor when `kip` is on.

## 13. UI

### 13.1 Placement
The KIP view replaces the output panel body, the same way the
interactive terminal does (`OutputPanel` stacked body). The panel's
header, status badge and stop/force-stop buttons stay.

### 13.2 Layout — wizard, one screen at a time
- Header: `hello.title` (fallback: command name) + program version.
- A compact, read-only **summary of answered steps** above the current
  screen (prompt title + chosen values; `secret` shown as `••••`).
- The current screen: prompt/confirm (if any) and its display blocks in
  arrival order.
- Footer: Back (when allowed), Cancel (unless `cancellable: false` on
  the open prompt or the latest progress), Submit.
- Terminal states — Unsupported, ProtocolError, Failed, Cancelled — are
  full-screen cards with icon, explanation, the Details drawer expanded,
  and "Run again". `done` shows its result card (§12.1).

### 13.3 Details drawer (collapsed by default)
Two tabs:
- **Log** — stderr + non-protocol stdout lines, monospace.
- **Protocol** — every message in both directions, one per line,
  direction-marked (→ / ←) and timestamped. `secret` values are replaced
  by `"••••"` in outgoing `response`/`change` lines.

It's the only place that looks like a terminal, and it's opt-in.

### 13.4 Own window (`kip_window`)
After start, Kai opens the KIP view in the detached output window
(`TerminalDrawer::showDetachedOutput`, the `kai -gw` mechanism).
**Only one interactive KIP view per session at a time**: while the
session is shown in the detached window, the docked panel shows a
placeholder card ("Running in its own window" + "Focus window"). Closing
the window moves the view back to the docked panel without touching the
process. Because the detached window creates a *new* `OutputPanel`
(`copyStateFrom`), the KIP view must be rebuildable at any time from the
session's state model (§18), never hold the only copy of the state.

**The main window stays put.** When the view goes to the own window (`kip_window`, or `-w` from
`kai -gw`) `MainWindow::startExternalRun` and `runCommandByName` (used by `kai run` and `kai.run()`) do
**not** call `showAndRaise()`; with the view docked they still do, because that is where it lives.

**Own-window size (`KipSettings::detachedWindowMode`, Settings → KIP).** `preference` (default) follows the
general window mode (Settings → Appearance → Open as); `normal` | `maximized` | `fullscreen` override it for KIP
views only (`TerminalDrawer::showDetachedWindowPerPreference` checks `m_panel->kipMode()`). Persisted as
`kip.detached_window_mode`; an unknown value falls back to `preference`.

**Auto-close (`kip_auto_close`, `kip_auto_close_delay_sec`).** When the view is in the
own window and the session ends with `KipOutcome::success`, `MainWindow` closes that window
after the delay (default 2 s, 0 = at once; clamped to 0–60) through
`TerminalDrawer::closeDetachedWindowFor(commandId)` — the view goes back to the docked
panel and the process is untouched. Failure or user cancel never closes it. A per-command
generation counter (`m_kipAutoCloseGeneration`) stops a pending close from hitting the window of
a re-run that started during the wait. Works the same for `kai -gw` (the window opened by the
CLI). Editor: KIP tab, "Close the window when it finishes" + a seconds spinbox (enabled only
with the checkbox). The validator knows both keys and warns on `kip_auto_close` without `kip`.

### 13.5 Command tree
KIP commands carry a small "KIP" badge next to the name (reuse the tinted
badge style), with a tooltip explaining it opens an app-like view.

### 13.6 Style rules
- All of Kai's texts through `utils::tr` (keys under `kip.*`, in both
  `en.json` and `pt.json`). Program-provided texts are rendered as-is.
- Every rounded element uses `utils::tokens::radius*` (AGENTS.md §9).
- Field widgets come from the shared field factory (§18), so KIP forms
  and the params dialog look identical.

## 14. Session lifecycle
States: `Handshaking → Running ⇄ AwaitingInput → Finished | Cancelled | Unsupported | ProtocolError | Failed`.

- **Cancel**: Kai sends `cancel`; after a **3 s** grace period, if the
  process is still alive, Kai calls `stop()` (SIGTERM → SIGKILL).
  `cancellable: false` hides Cancel; the panel's force-stop stays.
- **Exit while `AwaitingInput`** → Failed ("the program exited while
  waiting for input").
- **Final status comes from the exit code** (`ignoreExitCode` respected).
  `done` with a non-zero exit is still Failed; exit 0 without `done`
  shows a generic "Finished" card.
- The run goes through `ExecutionPipeline` like any shell command: run
  history, process list, stop/force stop, notifications on failure.

## 15. Compatibility
| Feature | With `kip: true` |
|---|---|
| `interactive_terminal`, `formatted_output`, `compact_output`, `open_last_link` | Mutually exclusive — disabled in the editor; ignored with a validator warning in files |
| `responders`, `capture_env` | Not allowed (compete for stdin/stdout); `declared_env_vars` stays usable for `set_env` |
| `is_background`, `cron_expression`, `auto_run` | Not allowed — editor disables them; files get a validator warning; the scheduler/auto-run skip the command with a log line |
| Used as a pre/post/cleanup hook | Rejected by the pipeline with a clear message; the hook picker hides KIP commands |
| `params`, `{{var}}`, env hierarchy, terminal target, `working_dir`, execution conditions, `ignore_exit_code` | Work as usual (terminal target forced to pipe mode, §3) |
| CLI **local** mode (`kai <path>` with a `kai.json`) | Refused: "KIP commands need the Kai app — use `kai -g`" (exit 1) |
| CLI **global** mode (`kai -g …`) | Runs in the app like a click; the app comes to the front with the KIP view focused (`-w` opens it in its own window). The client prints only log lines and returns the real exit code |

## 15b. Python/Node/PHP programs written in the command (injected `kip` module)
A command with `language: python|node|php` and `kip: true` has the program as its own text
(no file). Kai prepends a small client module (`src/engine/kip-modules/kip.py` /
`kip.js` / `kip.php`, compiled in as a Qt resource) before the code:
- Python: registered in `sys.modules['kip']` → `import kip`.
- Node: `globalThis.kip` and `require('kip')` (patched `Module._load`); the code runs in an
  async function so `await kip.prompt(...)` works at the top level.
- PHP: the class `Kip` (static methods; options are arrays with the protocol's keys, callbacks `onChange`/`onChip`/
  `validate` are closures in them; Back throws `KipBack`). The helper is a **separate `eval`** from the user's code,
  so a leading `namespace`/`declare(strict_types=1)` stays the first statement of its own unit; the class is
  declared inside `if (!class_exists('Kip', false))`, so loading it twice (inline + `auto_prepend_file`) is
  harmless. Compatible with PHP 7.4+. PHP has no `kai` module. Without KIP the injected `Kip` is a stub whose
  `__callStatic` throws the "turn KIP on" message (like the Python/Node stubs).

The modules are protocol clients only: `hello` goes out before the first message,
`prompt/confirm` return the answer (`values` / bool), `watch` changes are answered with
a `patch` echoing the `seq` (`on_change`/`onChange`), chips with `chip_result`
(`on_chip`/`onChip`), `validate` answers `invalid`, **Back** raises/rejects `kip.Back`,
**Cancel** (or Kai closing stdin) exits with code **130**. Everything else is a
one-to-one wrapper (`message`, `markdown`, `progress`, `steps`/`step`, `table`, `notify`,
`set_env`, `done`, plus `send(type, ...)` for anything else). Output is flushed after each
message and Node pauses + unrefs stdin while idle so the process ends after `done`.
Tests: `tests/test_command_language.cpp` drives the modules over real pipes.

### 15c. The modules on disk (`ModuleFiles`) — scripts that run as files
The injection above only reaches code that Kai itself hands to the interpreter. A script that runs as a **file**
(a user's `tool.py` called from a native `kip: true` command, a subprocess, the file `ScriptSpill` writes) never
sees `sys.modules`, so `ModuleFiles` (`src/engine/module-files.{h,cpp}`) also writes the modules to disk and
exports where they are:
- Folder `<temp>/kai-run/modules-<sha1 of the sources:10>/{kip,nokip}/` (`kip/` = real modules, `nokip/` = the stubs
  that explain what to turn on). It holds `kai.py kai.js kip.py kip.js kip.php` and `ini/kai.ini`
  (`auto_prepend_file="…/kip.php"`). Folders of other hashes (older Kai versions) are removed on `write()`.
- Env, built by `ModuleFiles::environment` and merged by `ExecutionPipeline::processEnvFor`, the Kai folder **in
  front of** the user's value (the value in the Kai env hierarchy, else Kai's own process env): `PYTHONPATH`,
  `NODE_PATH` (CommonJS `require` only; ESM `import` ignores it), `PHP_INI_SCAN_DIR` (`<sep><dir>/ini`: the blank first
  item keeps the default `conf.d`), and `KAI_MODULES` (the folder).
- `php -r` does **not** apply `auto_prepend_file` (a file or stdin does); `-r` code uses
  `require getenv('KAI_MODULES') . '/kip.php';`.
- Who gets them (`ExecutionPipeline::moduleFilesApply`): `type: command`, **no terminal profile** (the variables do
  not cross wsl.exe/docker/ssh; WSL has no `WSLENV` support here) and either a python/node/php command or a **native
  command with `kip: true`** — never a plain native command, so a project's own `kai`/`kip` package is not shadowed.
- Writing is async: `ensureAsync()` at app start-up, and `resolveCommandLine` writes in its worker thread when
  `ModuleFiles::needsRefresh()` (never verified, or more than 30 minutes ago: a /tmp cleaner may have taken the files).
  Tests: `tests/test_module_files.cpp`, `anExternalScriptFileFindsTheKipModule` in `test_command_language.cpp`.

## 16. `kai kip` helper (for shell authors)
Writing and parsing JSON in bash is the biggest adoption barrier. `kai
kip` is a **pure, offline** helper: no IPC, no running app, no config
read. It only formats messages to stdout and extracts values from a
response. Because it inherits the script's stdout, its output goes
straight to Kai.

```bash
#!/usr/bin/env bash
kai kip hello --title "Deploy"
kai kip prompt --id env --title "Where to?" \
  --field select env "Environment" --options dev,staging,prod --required \
  --field flags opts "Options" --flag force:"Force" --flag dry:"Dry run" \
  --field folderpick dir "Build folder"
read -r resp
[ "$(kai kip get "$resp" type)" = cancel ] && exit 130
env=$(kai kip get "$resp" values.env)
dry=$(kai kip get "$resp" values.opts.dry)      # true|false
kai kip progress 30 "Uploading..."
kai kip done --title "Deployed" --action open_url:"Open":https://example.com
```

- Verbs: `hello`, `prompt`, `confirm`, `patch`, `invalid`, `message`,
  `markdown`, `progress`, `steps`, `step`, `table`, `notify`, `set-env`,
  `done`, `raw '<json>'` (validates and emits), `get <json> <path>`.
- `prompt`/`patch` take a sequence of `--field <type> <name> [label]`;
  modifiers (`--options a,b`, `--option value:label`, `--flag
  name:label[:default]`, `--required`, `--default v`, `--placeholder`,
  `--description`, `--group`, `--watch`, `--multiple`, `--no-remember`,
  `--min/--max/--step/--decimals`, `--mode`, `--range`, `--filter`,
  `--initial-dir`, `--path-format`, `--column key:label`, `--row-key`,
  `--rows-json '<array>'`) apply to the **last** `--field`.
  Prompt-level: `--id`, `--title`, `--description`, `--submit-label`,
  `--back`, `--no-cancel`, `--no-remember`.
- `get` paths are dot-separated (`values.opts.dry`); strings print raw,
  numbers/bools as JSON literals, arrays one item per line, objects as
  compact JSON; a missing path prints nothing and exits 1.
- Always writes `\n` (never `\r\n`, also on Windows). Invalid usage →
  message on stderr, exit 2, **nothing** on stdout (stdout is the
  protocol channel).
- `kip` joins `core::reservedCliVerbs()` and the shell completion.

## 17. Error handling (complements spec 07)
- Malformed JSON, unknown types and invalid field definitions never crash
  or end the session: logged and skipped (a field without `name` is
  dropped; a prompt with no valid field still renders as an
  "acknowledge" screen).
- Writing to stdin after the process exited is a silent no-op (existing
  `ProcessRunner` behavior).
- Security: a KIP program cannot make Kai run anything. `open_url` is
  http(s)-only, `set_env` is allowlisted, Markdown links open externally
  only for http(s), remote images in Markdown are not loaded, secrets are
  redacted in the inspector, history and logs.

# Part B — Implementation plan

## 18. Architecture
| File | Responsibility |
|---|---|
| `src/core/kip-protocol.{h,cpp}` | Message/field structs (`std::variant`), `parseKipLine()` → `std::optional<KipMessage>` + diagnostics, serializers for Kai → program messages, limits. Zero widget dependency. |
| `src/core/kip-screen-state.{h,cpp}` | Pure model of what's on screen: header, answered summary, open prompt (with current values), display blocks, terminal state. Mutated by applying messages; every KIP view renders **from this**, which is what makes the detached window (§13.4) and rebuilds trivial. |
| `src/core/kip-cli-builder.{h,cpp}` | argv → message JSON and `get` path extraction for `kai kip` (pure, unit-testable). |
| `src/engine/kip-session.{h,cpp}` | Wraps a `ProcessRunner`: line reassembly, handshake timer, state machine, change/patch `seq` tracking, cancel grace timer, protocol trace. Signals: `stateChanged`, `screenChanged`, `notifyRequested`, `setEnvRequested`, `protocolTrace`, `logLine`. Slots: `submit(id, values)`, `sendChange(...)`, `back(id)`, `cancel()`. |
| `src/ui/shared/parameter-field-factory.{h,cpp}` | Field widgets extracted from `ParameterFormDialog` (text, textarea, select, multi-select list, file/folder pick, bool, number, date) + new ones (secret, single-choice list, table). Value get/set as `QJsonValue`, change signal, required check. |
| `src/ui/features/kip/kip-view.{h,cpp}` (+ small block widgets as needed: `kip-steps-block`, `kip-table-block`, `kip-done-card`, `kip-details-drawer`) | Renders `KipScreenState`, forwards user actions to the session. |
| `src/cli/cli-kip-helper.{h,cpp}` | `kai kip` entry point (thin, over `kip-cli-builder`). |

Touch points: `core::Command` (`kip`, `kipOpenInWindow`,
`kipLastValues`) + `toJson/fromJson`, `yaml-bridge`,
`kai-file-validator` (§15 warnings), `assets/manifesto/kai.schema.json`;
`ExecutionPipeline` (no PTY, env injection, hook rejection, session
creation); cron scheduler / auto-run (skip); `OutputPanel` (KIP body);
`TerminalDrawer` (detached placeholder); `MainWindow` (wiring, remember
values, notify, set_env, `-g` bring-to-front); command editor
(general/configuration tabs: checkboxes + exclusions; declared env vars
tab available); `CommandTreeWidget` (badge); hooks editor (hide KIP);
CLI local executor (refusal); `reservedCliVerbs` + completion.

## 19. Phases
Commit at the end of each phase on `experimental/kip`, with the build
green and that phase's tests passing.

1. **Protocol core** — `kip-protocol`, `kip-screen-state`.
   `test_kip_protocol`, `test_kip_screen_state`.
2. **Model** — `Command` fields, JSON/YAML round trip, schema, validator
   warnings. Extend `test_models`, `test_yaml_bridge`,
   `test_kai_file_validator`.
3. **Engine** — `KipSession`, pipeline integration (pipe mode, env vars,
   hook rejection, scheduler/auto-run skip). `sample/kip/` fixture
   scripts. `test_kip_session` covering: happy path, unsupported (exits
   on `--kip`), handshake timeout (inject a short timeout), wrapper noise
   before `hello`, malformed line mid-session, exit while awaiting input,
   cancel + grace, `invalid` round trip, change/patch with stale `seq`,
   patch timeout, `set_env` allowlist, line split across chunks.
4. **Field factory** — extract from `ParameterFormDialog` with **no
   behavior change** (`test_parameter_form_dialog` and the other form
   tests must pass untouched), then add secret, single-choice list and
   table widgets. `test_parameter_field_factory`.
5. **KIP view** — rendering, submit/back/cancel, remember values,
   details drawer + inspector with redaction, done actions, notify,
   `set_env` wiring, terminal-state cards, detached window + placeholder.
   `test_kip_view` (offscreen; drive it with a `KipScreenState` and with
   a fake session).
6. **Editor, tree, CLI** — editor checkboxes and exclusions, KIP badge,
   hook picker filter, local-mode refusal, `-g` bring-to-front, `kai kip`
   helper + completion. `test_kip_cli_builder`; extend
   `test_command_editor_dialog`, `test_cli_local_executor`,
   `test_cli_completion`.
7. **Samples, docs, polish** — `sample/kip/` demo apps for the 8 use
   cases (§1) written with `kai kip` (+ a `kai.json` wiring them, one
   with `kip_window`), `docs/kip.md` (protocol reference for program
   authors + helper reference), in-app help pages
   `assets/help/{en,pt}/kip.html`, README/docs index links,
   `test_i18n_sync` and `test_help_i18n` green, full test suite,
   `./build.sh`, visual check of every screen via offscreen
   `grab()` screenshots in both a light and a dark theme and all three
   corner styles.

## 20. Implementation notes & deviations
Decisions taken where the spec was ambiguous (most conservative reading
that fits the existing code), plus the places where the result deviates
from the letter of the spec. Updated at the end of every phase.

### Phase 1 — protocol core
- **Key order on the wire.** Messages are serialized with `QJsonObject`,
  which sorts keys alphabetically (`{"id":…,"kip":1,"type":"response",…}`).
  Any JSON parser is fine with that; the spec's examples are illustrative.
- **Unknown vs. malformed.** `parseKipLine` distinguishes four outcomes:
  not protocol (log), valid message, *unknown type* (ignored, warning) and
  *invalid* (known type, malformed body → skipped, warning). A line with a
  `kip` key whose value isn't a number, or without `type`, is *invalid*.
- **Field `remember`.** `remember: false` on the prompt **or** on the field
  turns remembering off; an explicit `true` on a field does not override a
  `false` prompt (the safe reading of "opt-out").
- **Remembered values are validated** with the same rule as `patch`
  (`kipIsValueValid`) and fall back to `default` otherwise.
- **Select with no value.** A select starts empty (`""`) unless it has a
  valid `default`; `required` therefore blocks submit until one is chosen.
- **`table` field answer summary** shows the first column's text of the
  chosen row (falls back to the row key) — friendlier than a bare id. The
  value sent to the program is always the row key.
- **Answered-steps summary** gets a step only when the *next* screen
  arrives, so a rejected answer (`invalid`) never shows up and the locked
  form is not duplicated by the summary right above it.
- **`done`** keeps the display blocks of the current screen (a `table`
  followed by `done` with a copy action must still show the table); it
  closes any open prompt. Failure/cancel/unsupported cards replace the body.
- **Limits** are applied while parsing. The 1 MiB line limit is counted in
  characters (UTF-16 units), which is exact for ASCII-dominated protocol
  traffic. Text values are cut at 64 KiB of UTF-8 without splitting a
  character.
- A `progress` without `value` is treated as indeterminate (same as
  `null`); values outside 0–100 are clamped.
- An action without `label` falls back to its URL/path/value so the button
  is never blank; `open_url` with a non-http(s) URL is rejected **at parse
  time** (so the UI can never see it).

### Phase 2 — model
- Persisted keys: `kip`, `kip_window`, `kip_last_values` (all omitted at
  their defaults). `kip_last_values` is a JSON object, not a
  string→string map: a remembered answer can be a number, array or object.
- Projects (`kai.json`/`kai.yml`) pick the keys up through
  `Command::fromJson`; the YAML bridge is generic and round-trips
  `"promptId/field"` keys (quoted automatically).
- Validator: warnings (never errors) for `kip` on a non-shell command, for
  every incompatible feature of §15 present together with `kip: true`, for
  `kip_window` without `kip`, and for a KIP command referenced from
  another command's `hooks`. `kip`/`kip_window` are known manifest keys.
- "Ignored" (§15) means non-destructive: the stored flags are kept as the
  author wrote them; the runtime simply gives `kip` priority.

### Phase 3 — engine
- **Where the logic lives.** `KipSession` owns the handshake, the state
  machine and the result of the run; `ExecutionPipeline` only creates it
  (one per run, `kipSessionFor(commandId)` / `kipSessionStarted`), forces pipe
  mode and reads `KipSession::outcome()` when the process finishes. The session
  connects to the runner *before* the pipeline does, so its outcome is ready
  when the pipeline's `finished` handler runs (Qt calls slots in connection
  order — commented at the call site).
- **Handshake.** The 10 s clock starts in `KipSession::start()` (i.e. at
  process start). Before `hello`, *any* protocol-looking line that isn't a valid
  `hello` — including an unknown type or a malformed body — is a `ProtocolError`
  ("first message wasn't hello"); plain/non-JSON lines are log noise.
- **Cancel vs. stop.** A user Cancel (or the panel's Stop/Force stop) that ends
  the process before a `done` arrives is *Cancelled*; the pipeline result is
  `success=false, stoppedByRequest=true` (no failure notification), exit code
  = the real one, or 130 if the program exited 0.
- **Failure rules (in order).** start failure → Failed; stopped by the user →
  Cancelled; exit while a prompt/confirm is open → Failed *even with exit code
  0*; crash → Failed; non-zero exit (unless `ignore_exit_code`) → Failed, also
  after a `done`; otherwise Finished. Unsupported/ProtocolError decided early keep
  their state and the run fails.
- **Remembered answers** are persisted only when the *next* screen arrives (or
  the session ends), so an answer the program rejects with `invalid` is never
  remembered. The session emits `answersRemembered`; the pipeline relays it as
  `kipAnswersRemembered(commandId, values)` for the UI to store in
  `Command::kipLastValues`.
- **`set_env`.** The session filters by `declaredEnvVars` (exact name match);
  the pipeline applies it through `exportDeclaredVar`, the same code that
  `ingestCapturedEnv` now uses, with the dynamic-variable scope captured at run
  start. Rejections are flagged in the protocol inspector and logged.
- **Kai's own log lines.** Parser/model diagnostics and session warnings are
  shown in the Details → Log tab prefixed with `[kai]`, so they go through
  `utils::tr` (keys `kip.diag.*`). They are also forwarded as log lines, so
  `kai -g` prints them next to the program's stderr. Raw protocol JSON is never
  forwarded to the log.
- **Environment.** `KIP_VERSION`/`KIP_LOCALE` are injected under the resolved
  env (user variables win) and also reach terminal-target wrappers (they go
  through the same `export`/`set` prefix as every other variable).
- **Compatibility at runtime.** For a `kip` command the pipeline ignores
  `is_background`, `capture_env` and `responders`; hooks that are KIP commands
  abort the pipeline with a clear message (cleanup hooks are skipped with a log
  line); `CronScheduler` and auto-run skip KIP commands with a log line;
  `ExecutionPipeline` in inherit-terminal mode (CLI local) refuses KIP.
- **Fixtures** live in `sample/kip/fixtures/` (raw-JSON scripts, no `kai kip`
  helper needed); `tests/test_kip_session.cpp` and `test_execution_pipeline.cpp`
  run them for real.

### Phase 4 — field factory
- **Extraction method.** The widget-building code of `ParameterFormDialog`
  moved, nearly verbatim, into `fields::make*` (text, textarea, JSON, select
  combo, check list with Space-toggle, switch, spin, path pick, date pick);
  the dialog now only composes them. `test_parameter_form_dialog` and every
  other form test pass **without any change** to their asserts. Extra proof:
  a throw-away harness rendered the *original* dialog (HEAD, renamed class) and
  the refactored one with 14 parameters of every type (groups, optional,
  multi-select, usage history, last values) and compared them: **0 of 810 000
  pixels differ**, same `values()` and `updatedUsageHistory()`.
- Things that stayed in the dialog on purpose: the collection chip picker (it
  talks to `CollectionSelectorDialog` and dialog state), the optional-field
  checkbox, grouping/grid layout and `isCompactParam`.
- The dialog's `eventFilter` (Space toggles a multi-select item) became a
  filter owned by the list itself, so KIP lists get it for free.
- **KIP-only widgets** (secret with show/hide toggle, single-choice list, data
  table, flags group, nullable number) live in the same factory. `KipFieldEditor`
  adapts a `core::KipField` to them: `value()/setValue()` as `QJsonValue`,
  `valueChanged` (any change) vs `userEdited` (user only — what triggers
  `watch`), `isFilled()` for `required`.
- **Select** gets an explicit empty entry ("—", or the field's `placeholder`)
  unless it has a valid default; typed text that isn't an option's label/value
  is "no choice" (the program never receives free typing from a select).
- **Number** is a `QDoubleSpinBox` whose minimum is one `step` below the real
  minimum and shows the "empty" text there: that is how `null` is represented.
  A value below the declared minimum clears the field.
- **Flags `required`** means "at least one flag on". **Table `required`** means
  "a row is selected". Filters appear above 8 options/rows, exactly as §6.
- Dates are held as `QDateTime` properties on the read-only line edit (same
  mechanism the parameter dialog already used) and serialized to the ISO
  formats of §6.
- Items with a `description` use a small delegate (muted second line); without
  descriptions the stock delegate is used, so parameter lists look as before.

### Phase 5 — KIP view
- **Rendering model.** `KipView` renders a `core::KipScreenState` and keeps no
  state of its own that can't be rebuilt from it: the detached window is just a
  second `KipView` bound to the same `KipSession` (`OutputPanel::copyStateFrom`
  hands the session over). Refreshes are coalesced (one per event-loop turn);
  blocks of the same kind/id are updated in place (a progress bar or checklist
  never flickers), and prompt fields are rebuilt only when their definition
  changed (a `patch` keeps the editors — and the focus — of untouched fields).
  `KipOpenScreen::serial` tells "same screen updated" from "a new prompt that
  happens to reuse the id".
- **Order inside a screen.** Display blocks are rendered above the open
  prompt/confirm. The model does not record the interleaving of blocks and the
  prompt, and in practice blocks are context that arrives first.
- **Group badge.** The pending-required count on a collapsed group is shown only
  while it is greater than zero (the parameters dialog dropped its always-on
  badge because it showed a meaningless "0"; §6 asks for the pending-required
  badge, so this satisfies both). A group containing a field with an error from
  `invalid` expands by itself.
- **Footer.** Back (when allowed) | Cancel | Submit. On a `confirm` screen the
  two answer buttons take the place of Submit and the session-level button is
  worded **"Cancel run"**, so it can't be mistaken for the default decline label
  ("Cancel"). Hidden when the session is terminal or a `done` is showing.
- **Keyboard.** Enter submits from any single-line field, spin box, check box
  or combo when Submit is enabled; it is left to the widget in textarea, list
  and table, and when a popup (combo/completer) is open. A `danger` confirm puts
  the focus on the decline button. Focus is moved to the first field of a new
  prompt only if the user isn't typing in another widget (Kai never steals focus).
- **`watch`.** Text-like fields debounce 300 ms; choice/date/flags/table send
  `change` at once. While a change is pending (including the debounce window) the
  Submit button is disabled.
- **Done actions.** `reveal` converts the path with `utils::convertFilePathFormat`
  into **this host's** native form only when `path_format` differs from it
  (`posix` on Windows → Windows path; `windows` on Linux/WSL → POSIX); a file
  reveals its folder. `copy` flashes "Copied" on the button. "Run again" is
  disabled until the process has exited (a `done` can arrive before the exit).
- **Terminal cards.** Unsupported / ProtocolError / Failed / Cancelled replace the
  screen content (the answered-steps summary stays above) and open Details once
  per state transition. Texts reuse the engine's `kip.error.*` messages.
- **Details.** Protocol direction markers: `←` = arrived from the program,
  `→` = sent by Kai. Entries Kai refused/ignored carry a `⚠` note.
- **Markdown.** `QTextBrowser` with every resource blocked (the override returns
  an empty image, which also stops Qt's own fallback to `file:`/`qrc:` URLs) and
  http(s)-only link activation. Link color is forced to the theme accent.
- **Notifications.** `notify` is recorded in the notification history like any
  other (event keys `kip_notify_<level>`) and raised as a toast only when the
  view isn't in front of the user: main window hidden/minimized/unfocused, a
  different command selected, or the output panel collapsed — the existing
  "notify even when focused" setting still applies. Only the global
  notifications switch gates it (there is no per-event toggle).
- **Selecting a KIP command in the tree** reconnects its session even when the
  program printed nothing to stderr (otherwise the finished state would vanish).
- **OutputPanel.** Pages 4 (view) and 5 (detached placeholder) of the body stack
  are created on the first KIP session, so panels that never show an app pay
  nothing; a theme refresh rebuilds both with the new tokens.
- **Spinner/state icons** are drawn with `QPainter` (no icon dependency); the
  result badge is a custom widget — a `QLabel` + stylesheet clipped the circle.

### Phase 6 — editor, tree, CLI
- **Editor.** A "KIP (app-like interface)" card in the Configuration tab holds
  "KIP interface" and "Open in its own window" (enabled only while KIP is on) plus
  a hint that Kai never edits the command. With KIP on, the competing controls
  (background, interactive terminal, formatted output, export
  variables switch, open last link, auto-run, cron) and the whole Auto-replies
  tab are **disabled with an explanatory tooltip**; their values are not erased,
  so switching KIP off before saving restores them. Saving with KIP on stores
  them as off/empty. "Ignore exit code", "hide on run", the terminal target and
  the Exportable variables tab stay available (`set_env` needs the latter).
  `kip_last_values` is preserved on edit like `last_param_values`.
- **Tree badge.** A custom item delegate draws a tinted "KIP" pill after the name
  (only for shell commands); a long name is elided to leave room for it; the badge
  has its own tooltip (the rest of the row doesn't).
- **Hook picker.** KIP commands are not offered (neither in the folder list nor in
  the expanded search). A KIP command already referenced as a hook disappears
  from the picker and is dropped on the next save (the pipeline would refuse it
  anyway).
- **CLI local mode** refuses a KIP command with `KIP commands need the Kai app —
  use \`kai -g\`` and exit code 1, before running anything; `--help` for such a
  command still works. In `-g` mode the app is brought to the front.
- **`kai kip`.** Dispatched in `main()` before `initCliProcess()` — that function
  reads the config, and the helper must be pure/offline. Its own messages use the
  language in `KIP_LOCALE` (set by Kai for the script), else English. stdout is
  written in binary mode (`\n` only, also on Windows); any usage error writes
  nothing to stdout, prints to stderr and exits 2.
  - Prompt-level `--id/--title/--submit-label/--back/--no-cancel` are accepted
    anywhere; `--description` and `--no-remember` before the first `--field`
    belong to the prompt, after it to the last field. Other field modifiers need
    a preceding `--field` (clear error otherwise).
  - `--option value:label[:description]`; `--flag name:label[:default]`;
    `--default` follows the field type (number → number, multiple list/table →
    comma list → array, flags → comma list of flags that start on); `--min/--max/
    --step/--decimals`, `--mode/--range`, `--filter/--initial-dir/--path-format`,
    `--column key:label`, `--row-key`, `--rows-json`.
  - Positional forms: `message [level] text`, `progress <0-100|null> [label]`,
    `step <steps-id> <item-id> <state> [detail]`, `notify <title> [text]`,
    `set-env NAME VALUE`, `markdown TEXT | --file PATH`; `steps` takes
    `--item <id> [label]` followed by `--state/--detail` for that item;
    `done --action type:label:value` (the value may contain colons) with
    `--path-format` applying to the last action.
  - `raw '<json>'` validates with the real parser and prints the normalized line.
  - `get '<json>' <path>`: dot path, numeric segments index arrays, arrays print
    one item per line, `null` prints `null`, a missing path prints nothing and
    exits 1.
  - Every output goes through `kipSerializeMessage`, so helper → parser round
    trips are covered by the protocol tests.
- `kip` joined `reservedCliVerbs()`, the top-level completion verbs and has its
  own completion (verbs, per-verb options, field types after `--field`).

### Phase 7 — samples, docs, polish
- **`sample/kip/`** is a normal importable project ("Kai KIP Demos", `kai.json` +
  README). It holds the eight use cases of §1 written with the `kai kip` helper
  (`deploy`, `db-restore`, `k8s-navigator`, `scaffold`, `onboarding`, `login`,
  `convert`, `lookup`), a hand-written `raw-protocol.sh` (no helper), `not-kip.sh`
  (a `kip: true` program that exits with `unknown option --kip` → the Unsupported
  screen) and a plain command that echoes `{{KAI_DEMO_TOKEN}}` after `login`.
  Onboarding is the one with `kip_window: true`. The project defines no root
  `cli_path` (so nothing collides with the reserved verb `kip`).
- **`invalid` keeps the prompt open**, so a script must *wait for the next
  `response`* and not send the prompt again: a new `prompt` with the same id
  replaces the screen and would wipe the errors the user was about to read. The
  first draft of three demos did exactly that; `test_kip_samples` caught it.
- **`test_kip_samples`** runs every demo for real (bash + the `kai` binary placed
  next to the tests as the helper) through `KipSession`, answering like a user
  (invalid name → fix, wrong password → retry, cascading `change`/`patch`, back
  from a danger confirm, `set_env` of the declared variable, unsupported). The
  execution tests are skipped if no `kai` binary sits beside the test binary; the
  `kai.json` validation (0 errors, 0 warnings) and the "script exists and is
  executable" check always run.
- **Docs.** `docs/kip.md` (protocol reference + helper), notes in `docs/cli.md`
  (local-mode refusal, `-g`/`-w`, `kai kip`), index entries in `docs/README.md`
  and `README.md`, and an in-app help topic `kip` (en/pt) — registered in
  `topicIds()` and in `test_help_i18n`'s mirrored list.
- **Flags used by the demos were checked against the helper**, not against the
  spec's examples: every line each script prints is validated with `kai kip raw`
  in the driver used during development.
- **Checked list items in the light theme.** The screenshot review found that a
  checked item of a check list (the KIP `flags` group, multi-select lists, and the
  parameters dialog's multi-select) looked *empty* on the light theme: the global
  rule `QListView/QTreeView::indicator:checked` drew a white tick (the qrc icon)
  on a transparent box. The rule now fills the box with the accent color
  (`app-stylesheet.cpp`). The same transparent-box rule exists for plain
  `QCheckBox::indicator:checked`; it was left alone because KIP does not use plain
  checkboxes (the app's own checkboxes are mostly switches) — worth a separate look.
- **Demos speak the protocol by hand (revised after the first Windows test).**
  The first version of the demos used the `kai kip` helper. That made the samples
  depend on the Kai binary being reachable from the *target* the command runs in
  (a WSL target on Windows does not see `kai.exe` unless it is on its `PATH`) —
  a program that needs Kai to run its own commands is the wrong default. The eight
  demos were rewritten as plain bash (`printf` for JSON, `sed` to read answers,
  four tiny functions at the top of each script); `test_kip_samples` now runs them
  with `PATH=/usr/bin:/bin` and fails if a script mentions `kai kip` or `kai.exe`.
  The helper itself (§16) is untouched and still documented; whether and how it
  ships is an open decision.
- **Scaffold/convert demos on Windows.** The first Windows test of `scaffold.sh`
  "succeeded" without creating anything visible: the folder picker returns
  `C:\Users\…`, which a WSL `bash` cannot use, and the script ignored `mkdir`
  failures. The pickers now declare `path_format: "posix"` (Kai hands the program
  `/mnt/c/…`), the reveal actions declare it too (Kai maps it back for the file
  manager), a failed `mkdir` ends in an error card with the system message, and the
  success card lists the created files. A path that only exists inside WSL (the
  default `/tmp/kai-kip-demo`) still cannot be revealed from Windows — Kai disables
  the button instead of opening a wrong folder.
- **WSL programs name their own paths (second Windows round).** With Kai on
  Windows and the program in WSL, `path_format: "posix"` is enough for picked
  folders (`C:\…` → `/mnt/c/…`), but Kai cannot guess where a WSL-only path
  (`/home/…`, `/tmp/…`) lives from the Windows side: `toWindowsPath` only swaps the
  slashes, so Reveal came out "Caminho não encontrado". The demos now convert their
  *own* reveal paths with `wslpath -w` (→ `C:\…` or `\\wsl.localhost\<distro>\…`;
  a no-op where `wslpath` does not exist). Kai itself stays WSL-agnostic; teaching
  it the distro of the WSL terminal target would be the alternative and is open.
  `convert.sh` also writes next to the input file (it used to write to `/tmp`, i.e.
  inside WSL, out of sight), asks before overwriting (`invalid`), reports a failed
  copy, and says that it copies instead of re-encoding.
- **Details start collapsed on every new session.** `KipView::setSession` collapses
  the drawer, so a failure of the previous run (which auto-opens it) or a click no
  longer leaves the log open on the next screen. Failures still open it on their
  own, once per state.
- **Kai maps WSL-only paths itself (implemented).** `utils::toWindowsPath` takes the
  WSL distro of the command's terminal target: `wslDistroFromTemplate` reads
  `-d/--distribution` from a `wsl[.exe]` template and, without it, falls back to
  `defaultWslDistro()` (Windows registry, `Lxss\DefaultDistribution`).
  `ExecutionPipeline` stores the result on the `KipSession` and `reveal` actions with
  `path_format: "posix"` turn `/home/u/x` into `\\wsl.localhost\<distro>\home\u\x`
  (`/mnt/c/…` is still `C:\…`). A target that is not WSL leaves the distro empty and
  the behaviour unchanged. The demos went back to plain `path_format: "posix"` and
  no longer call `wslpath`.

## 21. Chips and list navigation (addendum)

Added after the first round of real use (the `git quiver` picker): a prompt needed
ephemeral, inert actions next to its fields ("show the context of this branch",
"sync it", "delete it") and its list needed pages and a filter.

### 21.1 Chips
A **chip** is a small button under the fields of a `prompt`. Clicking it does **not**
answer the prompt: Kai tells the program, the program runs something and reports
back, and the outcome shows in a **box** under the chips. The step stays open.

`prompt.chips[]` (and `patch.chips[]`, which *replaces* the whole set when the key is
present — `[]` removes every chip):

| Key | Type | Description |
|---|---|---|
| `id` | string | Required, unique in the prompt. |
| `label` | string | Button text (default: the id). |
| `description` | string | Tooltip. |
| `icon` | string | A Lucide icon name from Kai's pool (optional). |
| `danger` | bool | Destructive look; also makes the confirmation button red. |
| `confirm` | `true` \| object | Ask first, **inside the box** (no modal): `{title?, text?, confirm_label?, cancel_label?}`. `true` = Kai's generic question. |
| `requires` | array of field names | The chip stays disabled until those fields are filled (a missing field never blocks). |

At most **24** chips per prompt.

Kai → program, when a chip actually runs (after the confirmation, if any):

```json
{"kip":1,"type":"chip","id":"pick","chip":"ctx","values":{"branch":"feat/login"}}
```
`values` is the same object a `response` would carry (secrets included, masked only in
the inspector).

Program → Kai, any number of times per run:

```json
{"kip":1,"type":"chip_result","chip":"ctx","state":"running","text":"Looking up the ticket…"}
{"kip":1,"type":"chip_result","chip":"ctx","state":"success","title":"Context · feat/login","text":"**Ticket** …"}
```
`state` is `running` (default), `success` or `error`; `title` defaults to the chip label;
`text` is Markdown (same restrictions as the `markdown` block); `id` (the prompt) is optional.

Rules:
- One run at a time: while a chip is confirming/running, all chips are disabled. A finished
  box (success or error) stays until the user closes it, runs another chip, or the screen changes.
- Chips are disabled while a `change` is waiting for its `patch`, while the prompt is locked
  (submitted) and in the static preview.
- A `chip_result` that matches no running chip (other chip, other prompt, already finished,
  still confirming) is ignored and logged. The box ignores updates after it was closed.
- A chip error never ends the session; only the program exiting does.
- Submitting, going back, or a new prompt clears the box.
- Chips are a `prompt` feature; a `confirm` screen has none.

### 21.2 List and table fields: filter and pages
`list` and `table` accept two more attributes:

| Key | Type | Description |
|---|---|---|
| `searchable` | bool | Show the filter box. Absent = automatic (only above 8 items); `true` forces it; `false` hides it. |
| `page_size` | int | `0`/absent = no pages. `N` = N items per page (max 200) with "‹ Page 2 of 5 · 47 items ›" below the list. |

Pages apply to the *filtered* result; changing the filter goes back to the first page.
The selection and the checked items of other pages are kept. The list keeps the height of
one page so the layout does not jump between pages; the bar is hidden when everything fits
on one page.

### 21.3 `kai kip` helper
- `--search`, `--no-search`, `--page-size N` (list/table).
- `prompt` / `patch`: `--chip ID [label]` opens a chip; the following `--chip-description T`,
  `--chip-icon NAME`, `--chip-danger`, `--chip-requires f1,f2`, `--chip-confirm` (generic
  question), `--chip-confirm-title T`, `--chip-confirm-text T`, `--chip-confirm-label L` and
  `--chip-cancel-label L` apply to the **last** chip. `patch --no-chips` empties the set.
- `kai kip chip-result <chip> <running|success|error> [text…] [--title T] [--id PROMPT]`.

### 21.4 Implementation notes
- Model: `KipOpenScreen::chips` + `chipRun` (`Confirming` → `Running` → `Success`/`Error`) in
  `KipScreenState`, so the detached window rebuilds the box like everything else.
  `KipSession::startChip / confirmChip / dismissChip` drive it; only the final "run" writes `chip` to stdin.
- UI: `KipChipBar` (a flow layout of pill buttons, radius from the corner tokens) and `KipChipBox`
  (tinted by phase: warning/danger while confirming, accent while running, success, error), in
  `ui/features/kip/kip-chips.*`. The confirmation is inline on purpose: it works the same in the
  docked panel and in the detached window and never blocks the event loop.
- The C++ member is `requiresFields` (`requires` is a C++20 keyword); the JSON key is `requires`.
- Pager: `fields::ListPager` filters and pages by hiding rows (data and selection untouched).
  The page height is recomputed when the list is shown/restyled, because the row height is only
  reliable after the final font and stylesheet are applied.
- A spontaneous `patch` is one WITHOUT `seq` (`KipPatch::spontaneous`): always applied, never ends a
  pending change. (Before, the only way was `seq: 0`, which counted as stale as soon as any `change` had
  been sent, so "filter field + chips that repaint the table" stopped repainting after the first filter.)
  The Python/Node modules expose it as `kip.patch(id, fields=, remove=, chips=)`. `sample/kip/table-browser.py`
  (demo 15) is the DynamoDB-style browser built on it and is covered by `test_kip_samples`.

## 22. Visual feedback round (post-release fixes)

Changes after trying KIP on a real desktop (Windows + WSL). They supersede the parts of
§13 they touch.

- **No "KIP" badge in the tree (§13.5 removed).** It looked odd next to the name; KIP
  commands are plain rows again. `kip.badge*` keys, the delegate and its test are gone.
- **No tab/actions bar on the output panel in KIP mode (§13.1 revised).** The bar with the
  "Saída" tab, clear/copy/export/options buttons and the status badge does nothing for an
  app-like view. It is hidden while a KIP session is shown, **except when the panel is
  collapsed** (then it is the only way to reopen it). "Open in its own window" moved to a
  small icon in the view header (`KipView::detachRequested` → `OutputPanel::kipDetachRequested`
  → `TerminalDrawer::detachOutput`); the detached window itself does not show it.
  Cancel lives in the footer of the view, as before.
- **Progress is transient.** A `progress` block disappears when `done` arrives or the session
  reaches a terminal state (an indeterminate bar kept animating on the result screen). Messages,
  Markdown, tables and checklists stay: they are part of the result.
- **Chips look like the standard buttons**: same surface, 1px border and corner radius token
  (`radiusMd`, the user's corner preference) as `QPushButton`; "danger" reuses the danger button
  colour; the running chip gets an accent border. The pager buttons use the same radius.
- **Detached output window = app window (applies to any detached output window, not only KIP).**
  New `AppWindowFrame` (`ui/shared/app-window-frame.*`): frameless, with the same custom title bar
  as the main window (logo, window title, minimize/maximize/close — shared
  `window-control-buttons.*`), corners from the user's preference (native DWM corners on Windows 11 via
  the shared `native-window-corners.*`, a region mask elsewhere, flat when maximized), drag by the
  title bar, double-click to maximize and resize from the edges. `TerminalDrawer::detachOutput` uses it;
  `TopUtilityBar` and `MainWindow` now share the same helpers.

## 23. Settings tab, shell-only editor and the AI manifestos

- **Settings → KIP** (new tab, `KipTab`; `core::KipSettings`, persisted as the `"kip"` object of
  `settings.json`): handshake timeout (2–120 s, default 10), `change` → `patch` timeout (2–120 s, default 10),
  cancel grace (1–30 s, default 3), remember last answers (default on) and "open Details on failure"
  (default on). They live in a small global (`core::kipSettings()`), set by `MainWindow` at start-up and when
  Settings are saved, and read when a session is created (`ExecutionPipeline::createKipSession`) and by
  `KipView`. "Remember" off ⇒ prompts are not pre-filled and new answers are not stored. A **Forget remembered
  answers** button clears `kip_last_values` of every command at once and persists immediately.
- **KIP is a shell-command feature.** The command editor offers it only in the Configuration tab of shell
  commands (the tab does not exist for HTTP) and `buildCommand()` never produces `kip`/`kip_window` for an HTTP
  command, even if a hand-edited file said so (covered by a test).
- **Manifestos.** `assets/manifesto/` (moved from `docs/manifesto/`, so the app can ship and read it) holds
  `kai-manifesto.md` (YAML-only), `kai-icons.md`, `kai.schema.json` and the new `kip-manifesto.md`: a self-contained
  guide that teaches an AI to write a KIP program — transport rules, handshake, every message and field,
  prompt/`invalid`/`watch`/chip behaviour, `done` and exit codes, `set_env`, `kai.yml` registration, templates
  (bash without helper or `jq`, Python), design guidance, testing and a generator checklist. A test parses every
  ` ```jsonl ` example in it with the real protocol parser, so the guide cannot drift from the implementation.
- **Help → AI manifestos** (`AiManifestosPage`): two cards with a *Copy to clipboard* button. The files are read
  on a worker thread (never on the UI thread) and copied whole; the `kai.json` one is copied together with the
  icon list (the manifesto tells the model to use only those names). `build.sh install` ships `assets/manifesto`.
- **KIP tab in the command editor (supersedes §20 "Phase 6 — Editor").** The KIP card left the Configuration
  tab. Shell commands get a **KIP** tab right after Configuration (hidden for HTTP, like Configuration): the
  *KIP interface* / *Open in its own window* switches and the hint; **Remembered answers** (how many answers
  Kai kept for this command, with a *Forget these answers* button that applies on OK); and **Exporting
  variables** (a shortcut to the Exportable variables tab, which `set_env` needs). The switches keep their
  object names (`adv_command_editor.kip`, `adv_command_editor.kip_window`) and the §15 exclusions still disable
  the competing controls in the Configuration tab. The global preferences stay in Settings → KIP.

## 24. A real program: `packaging/release.sh --kip`

Kai's release orchestrator got a `--kip` flag (and a *Publish a release (KIP)* command in `kai.yml`). Design:
the script already had three phases (questions → plan + confirmation → execution), so KIP replaces the I/O of
each one without touching the logic: the form fills the same `ARG_*` variables the flags fill (flags become the
form's defaults); phase 1 became a function (`resolve_plan`) that can run again on every attempt and in a
subshell as a *probe*, so every terminal validation (semver, existing tag, missing `gh`…) becomes an
`invalid`; the plan is a `confirm` with `danger:true` when it pushes or publishes and `back:true` to reopen
the form; the execution reports a `steps` checklist and ends in `done` (open the Release, reveal `dist/`,
copy the version) or an error card with the log tail. Details worth reusing: the protocol goes to fd 3 and
everything else to stderr; `docker compose run -T`; long children run in the background + `wait` so Cancel's
SIGTERM can kill them; stdin is redirected to `/dev/null` during execution so children cannot swallow
messages; `GIT_TERMINAL_PROMPT=0`. Without `--kip` the output and effects are byte-identical to the previous
script (checked by running both against the same sandboxes), and `tests/test_release_kip.cpp` drives the real
script in a throw-away repository with a local `origin` and fake `docker`/`gh`.

## 25. `kip` function injected into native shell commands

A **native** command with `kip: true` whose shell is POSIX (Linux, macOS, WSL and other POSIX targets) gets a `kip`
function with the **same verbs and options as `kai kip`** (§21.3), without the `kai` binary and without `jq`. (There
used to be `bash`/`sh`/`pwsh` languages for this; they were removed — the shell is `native`, and the old values
still load as `native`, with a validator warning.) The result is the same line `kai kip` would print
(`tests/test_kip_shell_helper.cpp` runs both on `tests/data/kip-helper-cases.json` and compares the normalised
messages; the C++ builder in `kip-cli-builder.cpp` is the reference).

- **Helper**: `src/engine/kip-modules/kip.sh` + `kip.awk` — POSIX `sh` + `awk` (mawk, gawk, busybox). The awk
  program is stored in `_KIP` (single-quoted: it must contain **no single quote**, a test enforces it) and called as
  `LC_ALL=C awk "$_KIP" <verb> <args>`. Extra verbs: `get <json> <path>` (JSON scanner in awk), `recv`
  (`read -r` into `$KIP_MSG`; exit 130 on `cancel`/EOF) and `raw` (syntax-only check). `hello` is sent first when the
  script did not.
- **Delivery**: `ExecutionPipeline::commandLineFor` puts `kipShellLoader()` — `eval "$(printf %s '<gzip+base64>' |
  base64 -d | gzip -dc)"` — before the (already interpolated) text, so `kip` is defined in the very shell that runs
  the command. `{{VAR}}` is replaced as in any native command. Outside a POSIX shell (cmd.exe / PowerShell) nothing
  is injected.
- **Very long texts**: see §26.
- **Limits** (documented in `docs/kip.md` §6.2 and the manifestos): needs `awk` (+ `base64`, `gzip` inline); the helper
  cannot use arrays/`local` and `kip recv` must not run in a subshell; `get` prints exponent numbers as written
  and drops NUL bytes; `raw` is syntax-only; helper messages are in English only.
- Spontaneous patches: `kai kip patch --seq` is optional (no `seq` = spontaneous, always applies).

## 26. Commands too long for the command line (`ScriptSpill`)

The operating system caps the command line: Windows `cmd.exe /c` cuts at 8191 characters (and a WSL target adds a
base64 of the line, ×4/3), Linux fails the `exec` above ~128 KB for one argument. Instead of asking the user to
move the text to a file, `ExecutionPipeline::resolveCommandLine` does it:

- Applies to a **native** command whose shell is POSIX and that can see the Kai host's disk: a local shell on Unix or
  a WSL distro on Windows (`spillEligible`). **Python/Node/PHP commands spill too** (`interpreterSpillEligible`: the
  local process — on Windows too, there the cmd.exe line gets the path in double quotes — or a WSL distro): the file
  is `buildInterpreterScript` (the very same bootstrap as the `-c`/`-e`/`-r` line, so it carries the modules and works
  wherever the file is visible even though the env does not reach WSL) as `kai-<hash>.py|cjs|php` (`.cjs`: CommonJS even
  under a `"type": "module"` package.json) and the line becomes `python3 -u '<path>'` / `node '<path>'` /
  `php '<path>'` (`ScriptSpill::quotedPath`). Docker/ssh targets keep the long line (and the existing
  `execution_pipeline.line_too_long` warning): the file would not exist on the other side.
- `ScriptSpill::exceedsLimit(size, wslTarget)` decides (Windows: estimated final size > 8000; Unix: > 100000).
- The script (the `kip` helper in plain text first, when the command has KIP; the interpolated text after) is written
  **in a worker thread** to `<temp>/kai-run/kai-<sha1:16>.sh` (LF line endings; same content, same file;
  `cleanupOldAsync()` at start-up removes files older than 3 days) and the command becomes `. '<path>'` — the path is
  translated for WSL (`/mnt/c/...`). Sourcing keeps the semantics of the inline text (functions, `exit`, `cd`).
- **The environment counts on a WSL target.** There the variables are not process variables: `buildTargetedCommand`
  writes `export K='V'` for the whole hierarchy into the command text (then base64 ×4/3 for `{{command_b64}}`, then
  `cmd.exe /c` at 8191). A standard JWT plus the rest of the hierarchy is enough to cut the line. `resolveCommandLine`
  therefore sizes `line + exports + 400` (cd, kill-remote prefix) with `ScriptSpill::exceedsLimit(…, true)`
  (`envEmbedEligible`: WSL distro + POSIX flavour) and, when it does not fit, writes the `export`s followed by the
  line to a `.sh` and returns `. '<path>'` with `envEmbedded = true`, so `applyTerminalProfile` does not add them again.
  That file can hold secrets: spill files are created `0600` (the default folder `0700`; older world-readable files are
  fixed when reused). Tests: `aBigTokenInTheEnvironmentDoesNotBlowTheWslCommandLine` (fake `wsl.exe`, 200 KB of env
  spread over 20 variables — one variable alone cannot pass 128 KB inside a Linux either), `filesAreReadableByTheOwnerOnly`.
- Any problem (the write fails, a quote in the path) leaves the command as it was. The process starts after the
  write finishes; when nothing needs to be written, `done` runs synchronously as before.

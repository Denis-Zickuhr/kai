# KIP — Kai Interface Protocol

KIP lets a command turn Kai's output panel into a small **app**: instead
of a wall of text, the program asks questions (forms, confirmations,
lists, tables, pickers), reports progress (bars, checklists) and ends with
a result card that has buttons. The program just prints JSON lines; Kai
draws the interface.

Use it for wizards (deploy, restore, scaffolding, onboarding), logins that
export a token, file conversion, lookups with a result table — anything
that today needs a chain of `read -p` prompts.

Working examples: [`sample/kip/`](../sample/kip/) (import it into Kai and
run the commands). Full specification: [`specs/11-kip-protocol.md`](../specs/11-kip-protocol.md).

---

## 1. Turning it on

In the command editor, the **KIP** tab (command-type commands only; it also lists the answers Kai remembered
for this command and links to the Exportable variables), or in
`kai.json` / `kai.yml`:

```json
{
  "name": "Deploy",
  "type": "command",
  "command": "./deploy.sh",
  "kip": true,
  "kip_window": false
}
```

- `kip: true` runs the command as a KIP session. Command-type commands only (not HTTP).
- `kip_window: true` opens the view in its own window instead of the
  output panel.
- `kip_auto_close: true` closes that window by itself once the session
  finishes **successfully** (it needs `kip_window`, or the window opened by
  `kai -gw`). Made for quick scripts started from the terminal: the window
  appears, does its job and goes away. A failure or a cancel keeps the window
  open — that is where the error is. `kip_auto_close_delay_sec` (default `2`,
  `0` = at once) is how long the result card stays up before it closes.
- Kai **never edits your command line**. If the tool needs a flag
  (`mytool deploy --kip`), write it in the command yourself.
- The params form (`params`) still runs first; `{{var}}` works in the
  command, but never inside protocol messages.
- Kai sets `KIP_VERSION=1` and `KIP_LOCALE=pt|en` in the process
  environment, so the program can localize its own texts.
- A KIP command cannot also be background, interactive-terminal,
  formatted-output, Markdown-rendered, scheduled (`cron`), auto-run, have
  auto-replies or `capture_env`, and cannot be used as a hook. The editor
  disables those controls; files get validator warnings (`kai validate`).
  `declared_env_vars` stays available (see `set_env`).

## 2. Transport

JSON Lines, UTF-8, one object per line.

| Channel | Direction | Content |
|---|---|---|
| stdout | program → Kai | protocol messages |
| stdin | Kai → program | Kai's answers |
| stderr | — | free-form log (shown in **Details → Log**) |

Every message is `{"kip": 1, "type": "<type>", ...}`. A stdout line that
is not a JSON object with a `kip` key is **not** protocol: it goes to the
log, so tools that print a banner first still work. Lines longer than
1 MiB are dropped. No PTY is used; the process always runs on pipes.

The first protocol message must be `hello`. If the process exits (or 10 s
pass) before a `hello`, Kai shows **"This command doesn't support KIP"**
with the log open. A first message that isn't `hello`, or a `hello.kip`
newer than Kai speaks, is a protocol error.

## 3. Messages from the program

| `type` | Fields | Effect |
|---|---|---|
| `hello` | `title?`, `version?` | Handshake. `title` names the app. |
| `prompt` | `id`, `title?`, `description?`, `submit_label?`, `back?`, `cancellable?`, `remember?`, `fields[]`, `chips?[]` | Form; waits for a `response`. Chips: see §5.1. |
| `confirm` | `id`, `title?`, `text`, `danger?`, `confirm_label?`, `cancel_label?`, `back?`, `cancellable?` | Yes/no. Answer: `values.confirmed`. |
| `patch` | `id`, `seq`, `fields[]`, `remove?[]`, `chips?[]` | Updates fields (and, if `chips` is present, replaces the chips) of the open prompt. |
| `invalid` | `id`, `errors{field: msg}`, `message?` | Keeps prompt `id` open and shows errors. |
| `message` | `level` (`info` `success` `warning` `error`), `text` | Status card. |
| `markdown` | `text` | Rendered Markdown (http/https links only, no remote images). |
| `progress` | `value` (0–100 or `null`), `label?`, `cancellable?` | Progress bar; the next `progress` updates it. |
| `steps` | `id`, `title?`, `items[]` of `{id, label, state?, detail?}` | Checklist. |
| `step` | `steps`, `id`, `state`, `detail?` | Updates one item. States: `pending` `running` `success` `error` `skipped`. |
| `table` | `id?`, `title?`, `columns[]` of `{key,label}`, `rows[]` | Read-only table (select a cell, Ctrl+C). |
| `notify` | `title`, `text?`, `level?` | System notification (respects Kai's notification settings). |
| `set_env` | `name`, `value` | Exports a dynamic variable — see §7. |
| `chip_result` | `chip`, `state?`, `title?`, `text?`, `id?` | Progress/outcome of a running chip (§5.1). |
| `done` | `title?`, `text?`, `level?`, `actions?[]` | Result screen. Exit right after. |

Rules: unknown `type`s are ignored (logged); a `response`/`patch`/`invalid`
for an id that isn't open is ignored; a new `prompt` replaces the open
one. Limits: 100 fields per prompt, 5000 options, 1000 table rows, 200
checklist items, 64 KiB per text.

### Result actions

`done.actions[]` become buttons next to **Run again**:

| `type` | Fields | Behavior |
|---|---|---|
| `open_url` | `label`, `url` | Opens in the browser. **http/https only.** |
| `reveal` | `label`, `path`, `path_format?` | Shows the file/folder in the file manager. |
| `copy` | `label`, `value` | Copies to the clipboard. |

## 4. Messages from Kai

| `type` | Fields | When |
|---|---|---|
| `response` | `id`, `values{}` | The user submitted prompt/confirm `id`. |
| `change` | `id`, `seq`, `field`, `values{}` | A `watch` field changed. |
| `back` | `id` | The user pressed Back. |
| `chip` | `id`, `chip`, `values{}` | The user ran a chip (§5.1). The prompt stays open. |
| `cancel` | — | The user pressed Cancel. Exit promptly; after 3 s Kai stops the process. |

## 5. Fields

Common attributes: `name` (required, unique), `label`, `description`,
`required`, `default`, `placeholder`, `group`, `watch`, `remember`.

| `type` | Extra attributes | Value in `response.values` |
|---|---|---|
| `text` | — | string |
| `secret` | — (masked, never remembered, hidden in the inspector) | string |
| `textarea` | — | string |
| `number` | `min`, `max`, `step`, `decimals` | number or `null` |
| `date` | `mode` (`date` `time` `datetime`), `range` | ISO string; with `range`: `{start, end}` |
| `select` | `options[]` | string |
| `list` | `options[]`, `multiple`, `searchable`, `page_size` | string, or array with `multiple` |
| `table` | `columns[]`, `rows[]`, `row_key` (default `id`), `multiple`, `searchable`, `page_size` | the row key, or array |
| `filepick` | `filter`, `initial_dir`, `path_format` | string |
| `folderpick` | `initial_dir`, `path_format` | string |
| `flags` | `options[]` of `{name,label,description?,default}` | `{name: bool, ...}` |

`options` accepts plain strings or `{value, label?, description?}`. A field
left empty is sent as its empty value (`""`, `[]`, `false` per flag,
`null`). `required` is checked by Kai before Submit; everything else is
yours to check — answer with `invalid`.

**Long lists.** For `list` and `table`, `searchable: true` always shows the filter box
(by default it appears only above 8 items; `false` hides it) and `page_size: N` shows N items per
page with a "‹ Page 2 of 5 · 47 items ›" bar. Pages apply to the filtered result, and what is selected
or checked on other pages is kept.

**Remembered answers.** Kai pre-fills each field with what the user
submitted last time (same command, prompt `id` and field `name`), ahead of
`default`. Opt out with `"remember": false` on the prompt or a field.

**Dynamic fields.** A `watch: true` field sends a `change` (text is
debounced 300 ms). Answer **every** `change` with a `patch` that echoes
its `seq` (empty `fields` is fine). Each patched field replaces the one
with the same name (or is added); `remove` drops fields. Submit stays
disabled until the patch arrives (10 s at most).

### 5.1 Chips

A **chip** is a small button under the fields of a prompt for an *ephemeral, inert* action: "show the
context of this branch", "sync it", "delete it". Clicking one does **not** answer the prompt: Kai sends
`chip`, your program does the work and reports with `chip_result`, and the outcome appears in a box under
the chips. The step stays open.

```json
{"kip":1,"type":"prompt","id":"pick","fields":[{"name":"branch","type":"list","options":["main","feat/x"]}],
 "chips":[
   {"id":"ctx","label":"Context","icon":"info","requires":["branch"]},
   {"id":"delete","label":"Delete","danger":true,"requires":["branch"],
    "confirm":{"text":"This cannot be undone.","confirm_label":"Delete","cancel_label":"Keep"}}]}
```

| Chip key | Meaning |
|---|---|
| `id` | required, unique in the prompt |
| `label`, `description` (tooltip), `icon` (a Kai icon name) | looks |
| `danger` | destructive look; the confirmation button turns red |
| `confirm` | `true`, or `{title?, text?, confirm_label?, cancel_label?}`: Kai asks **inside the box** before sending anything |
| `requires` | field names that must be filled before the chip is enabled |

When the chip runs, Kai sends `{"type":"chip","id":"pick","chip":"ctx","values":{…current values…}}`.
Answer with as many `chip_result` messages as you like — `{"type":"chip_result","chip":"ctx","state":"running","text":"…"}`
while working, then `success` or `error` with a Markdown `text` (and optionally a `title`). An `error` does not end
the session. One chip runs at a time; the box stays until the user closes it or the screen changes.

## 6. The `kai kip` helper

Writing JSON from bash is painful, so `kai kip` formats it for you. It is
**pure and offline** — no Kai app, no config — and prints one message per
call on stdout.

```bash
#!/usr/bin/env bash
kai kip hello --title "Deploy"
kai kip prompt --id env --title "Where to?" \
  --field select env "Environment" --options dev,staging,prod --required \
  --field flags opts "Options" --flag force:"Force" --flag dry:"Dry run"
read -r resp
[ "$(kai kip get "$resp" type)" = cancel ] && exit 130
env=$(kai kip get "$resp" values.env)
dry=$(kai kip get "$resp" values.opts.dry)        # true | false
kai kip progress 30 "Uploading…"
kai kip done --title "Deployed" --action open_url:"Open":https://example.com
```

| Verb | Usage |
|---|---|
| `hello` | `[--title T] [--version V]` |
| `prompt` | `--id ID [--title T] [--description D] [--submit-label L] [--back] [--no-cancel] [--no-remember] --field <type> <name> [label] …` |
| `confirm` | `--id ID --text TEXT [--title T] [--danger] [--confirm-label L] [--cancel-label L] [--back] [--no-cancel]` |
| `patch` | `--id ID --seq N [--field …] [--remove a,b] [--chip … \| --no-chips]` |
| `invalid` | `--id ID [--error field=message]… [--message TEXT]` |
| `message` | `[info\|success\|warning\|error] TEXT` |
| `markdown` | `TEXT` or `--file PATH` |
| `progress` | `<0-100\|null> [label] [--no-cancel]` |
| `steps` | `--id ID [--title T] --item ID [label] [--state S] [--detail D] …` |
| `step` | `<steps-id> <item-id> <state> [detail]` |
| `table` | `[--id ID] [--title T] --column key:label … --rows-json '[…]'` |
| `notify` | `TITLE [TEXT] [--level L]` |
| `set-env` | `NAME VALUE` |
| `done` | `[--title T] [--text X] [--level L] [--action type:label:value]…` |
| `chip-result` | `<chip> <running\|success\|error> [text…] [--title T] [--id PROMPT]` |
| `raw` | `'<json>'` — validates a message and prints it |
| `get` | `'<json>' <path>` — reads a value from a response |

**Field modifiers** apply to the *last* `--field`: `--required`,
`--default V`, `--placeholder T`, `--description T`, `--group G`,
`--watch`, `--no-remember`, `--options a,b,c`, `--option value:label[:description]`,
`--multiple`, `--flag name:label[:default]`, `--min/--max/--step/--decimals`,
`--mode`, `--range`, `--filter`, `--initial-dir`, `--path-format`,
`--column key:label`, `--row-key K`, `--rows-json '[…]'`, `--search`, `--no-search`, `--page-size N`.
Before the first `--field`, `--description` and `--no-remember` belong to
the prompt.

**Chip options** (`prompt` and `patch`): `--chip ID [label]` starts a chip; `--chip-description T`,
`--chip-icon NAME`, `--chip-danger`, `--chip-requires f1,f2`, `--chip-confirm`, `--chip-confirm-title T`,
`--chip-confirm-text T`, `--chip-confirm-label L` and `--chip-cancel-label L` apply to the last chip.

**`get` paths** are dot-separated (`values.opts.dry`, `values.tags.0`).
Strings print raw, numbers and booleans as JSON literals, arrays one item
per line, objects as compact JSON. A missing path prints nothing and exits 1.

Invalid usage writes a message to **stderr**, nothing to stdout, and exits
2 — stdout is the protocol channel and never gets garbage. Output always
ends in `\n`.

Any language works the same way without the helper: print the JSON lines
yourself (see `sample/kip/raw-protocol.sh`).

## 6.1 Writing a KIP program in Python or Node

Set the command's **Language** to Python or Node (see [commands.md](commands.md)) and turn **KIP** on:
the text of the command is the program itself — no file to ship, no JSON to print by hand. Kai injects a
small `kip` module (Python: `import kip`; Node: the global `kip`, or `require('kip')`) and it sends `hello`
before your first message, so a whole wizard is a few lines:

```python
import kip

values = kip.prompt([
    {"name": "env", "type": "select", "label": "Environment", "options": ["dev", "prod"], "required": True},
], id="where", title="Where to?")

if values["env"] == "prod" and not kip.confirm("Deploy to production?", danger=True):
    kip.done(title="Cancelled", level="warning")
else:
    kip.progress(50, "Deploying…")
    kip.done(title="Deployed to " + values["env"])
```

```js
const values = await kip.prompt([
  { name: 'env', type: 'select', label: 'Environment', options: ['dev', 'prod'], required: true },
], { id: 'where', title: 'Where to?' });

kip.progress(50, 'Deploying…');
kip.done({ title: 'Deployed to ' + values.env });
```

| Python | Node | Does |
|---|---|---|
| `kip.prompt(fields, id=, title=, on_change=, on_chip=, validate=, ...)` | `await kip.prompt(fields, {id, title, onChange, onChip, validate, ...})` | Shows the form, returns the `values` dict/object. Other options are the prompt keys of §3 (`description`, `submit_label`, `back`, `cancellable`, `remember`, `chips`). |
| `kip.confirm(text, id=, danger=, ...)` | `await kip.confirm(text, {id, danger, ...})` | Yes/no → `True`/`False`. |
| `kip.message(text, level="info")`, `kip.markdown(text)` | `kip.message(text, level)`, `kip.markdown(text)` | Status card / Markdown. |
| `kip.progress(value, label)` | `kip.progress(value, label)` | `value` 0–100 or `None`/`null`. |
| `kip.steps(id, items)`, `kip.step(steps_id, id, state, detail)` | `kip.steps(id, items)`, `kip.step(stepsId, id, state, detail)` | Checklist. |
| `kip.table(columns, rows)` | `kip.table(columns, rows, {id, title})` | Read-only table. |
| `kip.notify(...)`, `kip.set_env(name, value)` | `kip.notify(...)`, `kip.setEnv(name, value)` | Notification / exported variable (§7). |
| `kip.done(title=, text=, level=, actions=)` | `kip.done({title, text, level, actions})` | Result screen. |
| `kip.send(type, **fields)`, `kip.log(...)` | `kip.send(type, fields)`, `kip.log(...)` | Any message / a line for **Details → Log**. |

- `validate(values)` returns `{field: message}` to keep the form open (Kai gets an `invalid`); `on_change(field,
  values)` returns the fields to patch (the `seq` is echoed for you); `on_chip(chip, values)` returns a text or
  `{state, title, text}` (an exception becomes an `error` result).
- **Cancel** exits the process with code 130. **Back** raises `kip.Back` (Python) / rejects with `kip.Back` (Node).
- Node code runs inside an async function, so top-level `await` works.
- Output is unbuffered and UTF-8, so there is no flush to forget. Variables and parameters come from
  `os.environ` / `process.env` (`{{VAR}}` is not replaced in Python/Node code).

## 6.1 Writing a KIP program in Python or Node

Set the command's **Language** to Python or Node (see [commands.md](commands.md)) and turn **KIP** on:
the text of the command is the program itself — no file to ship, no JSON to print by hand. Kai injects a
small `kip` module (Python: `import kip`; Node: the global `kip`, or `require('kip')`) and it sends `hello`
before your first message, so a whole wizard is a few lines:

```python
import kip

values = kip.prompt([
    {"name": "env", "type": "select", "label": "Environment", "options": ["dev", "prod"], "required": True},
], id="where", title="Where to?")

if values["env"] == "prod" and not kip.confirm("Deploy to production?", danger=True):
    kip.done(title="Cancelled", level="warning")
else:
    kip.progress(50, "Deploying…")
    kip.done(title="Deployed to " + values["env"])
```

```js
const values = await kip.prompt([
  { name: 'env', type: 'select', label: 'Environment', options: ['dev', 'prod'], required: true },
], { id: 'where', title: 'Where to?' });

kip.progress(50, 'Deploying…');
kip.done({ title: 'Deployed to ' + values.env });
```

| Python | Node | Does |
|---|---|---|
| `kip.prompt(fields, id=, title=, on_change=, on_chip=, validate=, ...)` | `await kip.prompt(fields, {id, title, onChange, onChip, validate, ...})` | Shows the form, returns the `values` dict/object. Other options are the prompt keys of §3 (`description`, `submit_label`, `back`, `cancellable`, `remember`, `chips`). |
| `kip.confirm(text, id=, danger=, ...)` | `await kip.confirm(text, {id, danger, ...})` | Yes/no → `True`/`False`. |
| `kip.message(text, level="info")`, `kip.markdown(text)` | `kip.message(text, level)`, `kip.markdown(text)` | Status card / Markdown. |
| `kip.progress(value, label)` | `kip.progress(value, label)` | `value` 0–100 or `None`/`null`. |
| `kip.steps(id, items)`, `kip.step(steps_id, id, state, detail)` | `kip.steps(id, items)`, `kip.step(stepsId, id, state, detail)` | Checklist. |
| `kip.table(columns, rows)` | `kip.table(columns, rows, {id, title})` | Read-only table. |
| `kip.notify(...)`, `kip.set_env(name, value)` | `kip.notify(...)`, `kip.setEnv(name, value)` | Notification / exported variable (§7). |
| `kip.done(title=, text=, level=, actions=)` | `kip.done({title, text, level, actions})` | Result screen. |
| `kip.send(type, **fields)`, `kip.log(...)` | `kip.send(type, fields)`, `kip.log(...)` | Any message / a line for **Details → Log**. |

- `validate(values)` returns `{field: message}` to keep the form open (Kai gets an `invalid`); `on_change(field,
  values)` returns the fields to patch (the `seq` is echoed for you); `on_chip(chip, values)` returns a text or
  `{state, title, text}` (an exception becomes an `error` result).
- **Cancel** exits the process with code 130. **Back** raises `kip.Back` (Python) / rejects with `kip.Back` (Node).
- Node code runs inside an async function, so top-level `await` works.
- Output is unbuffered and UTF-8, so there is no flush to forget. Variables and parameters come from
  `os.environ` / `process.env` (`{{VAR}}` is not replaced in Python/Node code).
- The same command also gets the **`kai` module** to talk to the Kai app (notifications, environments, processes,
  running other commands) — see [languages.md](languages.md).
- With KIP **off**, `import kip` still works but any use of it raises an error that says to turn KIP on for the command.

## 7. Exporting variables (`set_env`)

`set_env` only works for names listed in the command's **Exportable
variables** (`declared_env_vars`); anything else is refused and flagged in
the inspector. Scope and persistence come from the declaration, not the
message. The value is available immediately to other commands as
`{{NAME}}`.

```json
{ "name": "Sign in", "command": "./login.sh", "kip": true,
  "declared_env_vars": [ { "name": "KAI_DEMO_TOKEN" } ] }
```

## 8. The view

- One screen at a time, with a compact summary of the answers so far above
  it (`secret` shown as `••••`).
- Footer: **Back** (when the program allows it), **Cancel run**, **Submit**.
  Enter submits; a `danger` confirm focuses the decline button so Enter
  never confirms by accident.
- **Details** (collapsed by default): *Log* (stderr and non-protocol
  stdout) and *Protocol* (every message in both directions, `←` from the
  program, `→` from Kai, secrets masked).
- Failure, cancellation, unsupported and protocol-error states are full
  cards with the details open and a **Run again** button.
- With `kip_window`, the view lives in its own window and the panel shows
  "Running in its own window"; closing the window brings it back.
- The output panel's own tab bar is hidden for KIP commands; the small pop-out icon in the top-right of the view opens it in its own window.
- **Chips** sit under the fields; their confirmation, progress and result share one box under them.

## 9. Command line

- `kai -g <command>` runs a KIP command like a click: the app comes to the
  front on the KIP view. With `-w` (or `kip_window: true`) the view opens in
  its own window and the **main window is left alone** — it is not shown or
  raised. The same goes for `kai run` and `kai.run()` on a `kip_window`
  command. The client
  prints the log lines and returns the real exit code.
- Local mode (`kai <path>` against a `kai.json`) **refuses** KIP commands
  (`KIP commands need the Kai app — use kai -g`, exit 1): there is no UI to
  render them.
- `kai kip …` is the helper from §6; `kai kip --help` lists everything.

## 10. A real example: `packaging/release.sh --kip`

Kai's own release script (version bump, build, commit, tag, push, GitHub Release) speaks KIP when you pass
`--kip`; the *Publish a release (KIP)* command in Kai's `kai.yml` does exactly that. One form replaces the
terminal questions (the `--bump/--build/--tag…` flags become its initial values), the plan is a confirmation
that turns **red** when it pushes or publishes, and the run is a live checklist whose failures show the tail of
the log. Without `--kip` the script behaves exactly as before. It is also a good reference for turning an
existing interactive script into a KIP program: the protocol is written by hand in the script, all
non-protocol output goes to stderr (Details → Log), the `docker compose run` gets `-T` (no TTY), and the
validations of the terminal flow are reused to produce `invalid`.

## 11. Settings and AI manifestos

**Settings → KIP** holds the global preferences: how long to wait for the program's first message
(default 10 s), for the `patch` that answers a field update (10 s) and after **Cancel** (3 s);
whether to remember the last answers (and a button to forget them all); and whether the Details
drawer opens by itself when a command fails; and **Own-window size** — how the view opens when it runs
in its own window: *follow the general window setting* (Settings → Appearance → Open as), *normal window*,
*maximized* or *fullscreen* (it decides only for KIP views, never for the main window). Whether a given command uses KIP is set in *its* editor
(its **KIP** tab), and only command-type commands (not HTTP) have that option.

**Help → AI manifestos** copies, with one click, the two guides meant to be handed to an AI assistant:
the [`kai.json` manifesto](../assets/manifesto/kai-json-manifesto.md) (with the icon list) and the
[KIP manifesto](../assets/manifesto/kip-manifesto.md), which teaches the model to write a KIP program
(protocol, templates in bash and Python, pitfalls, testing and a checklist).

## 12. Security

A KIP program cannot make Kai run anything: `open_url` accepts only
http/https, `set_env` is allowlisted, Markdown links open externally only
for http/https and remote images are never fetched, and secret values are
redacted in the inspector, history and logs.

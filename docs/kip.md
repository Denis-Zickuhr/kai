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
`kai.yml`:

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
| `progress` | `value` (0–100 or `null`), `label?`, `cancellable?`, `id?` | Progress bar; the next `progress` with the same `id` updates it. Several `id`s = several bars, stacked. |
| `steps` | `id`, `title?`, `items[]` of `{id, label, state?, detail?}` | Checklist. |
| `step` | `steps`, `id`, `state?`, `detail?`, `log?` | Updates one item. States: `pending` `running` `success` `error` `skipped`. `log` appends lines to the item's output (§5.5); without `state`/`detail` they stay as they were. |
| `table` | `id?`, `title?`, `columns[]` of `{key,label,format?}`, `rows[]`, `row_key?`, `row_actions?[]` | Read-only table (select a cell, Ctrl+C). Cell formats and row actions: §5.2. |
| `diff` | `id?`, `title?`, `patch` **or** `left`+`right` (`{name?, text}`), `language?`, `view?`, `context?`, `ignore_whitespace?`, `collapse_after?` | A diff: the output of `git diff`, or two texts compared by Kai (§5.3). |
| `kv` | `id?`, `title?`, `items[]` of `{key, value, format?, copy?}` | Label/value pairs (§5.4). |
| `stats` | `id?`, `items[]` of `{label, value, detail?, tone?}` | Up to 8 big numbers, 4 per row (§5.4). |
| `code` | `id?`, `title?`, `language?`, `text`, `highlight?[]`, `max_lines?`, `line_numbers?` | A code/config snippet with line numbers and a copy button (§5.4). |
| `notify` | `title`, `text?`, `level?` | System notification (respects Kai's notification settings). |
| `set_env` | `name`, `value` | Exports a dynamic variable — see §7. |
| `chip_result` | `chip`, `state?`, `title?`, `text?`, `id?` | Progress/outcome of a running chip (§5.1). |
| `done` | `title?`, `text?`, `level?`, `actions?[]` | Result screen. Exit right after. |

Rules: unknown `type`s are ignored (logged); a `response`/`patch`/`invalid`
for an id that isn't open is ignored; a new `prompt` replaces the open
one. A `diff`, `kv`, `stats` or `code` with the `id` of one already on the screen replaces it. Limits: 100 fields per
prompt, 5000 options, 1000 table rows, 200 checklist items, 64 KiB per text — 512 KiB for a diff patch, each diff side
and a code text (cut at the last whole line, with a "truncated" note) — 5000 tree nodes, 8 row actions, 200 `kv`
items, 8 `stats` and the last 200 lines of each step log.

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
| `chip` (row action) | `id`, `chip`, `field` **or** `table`, `row`, `values{}` | The user ran a row action (§5.2) on row key `row` of table field `field` (or table block `table`). |
| `cancel` | — | The user pressed Cancel. Exit promptly; after 3 s Kai stops the process. |

## 5. Fields

Common attributes: `name` (required, unique), `label`, `description`,
`required`, `default`, `placeholder`, `group`, `watch`, `remember`.

| `type` | Extra attributes | Value in `response.values` |
|---|---|---|
| `text` | `max_length` | string |
| `secret` | — (masked, never remembered, hidden in the inspector) | string |
| `textarea` | `max_length` (a "41 / 72" counter; over the limit, Submit stays off) | string |
| `number` | `min`, `max`, `step`, `decimals` | number or `null` |
| `date` | `mode` (`date` `time` `datetime`), `range` | ISO string; with `range`: `{start, end}` |
| `select` | `options[]`, `style` (`cards` \| `segmented`; default: a dropdown) | string |
| `list` | `options[]`, `multiple`, `searchable`, `page_size` | string, or array with `multiple` |
| `table` | `columns[]`, `rows[]`, `row_key` (default `id`), `multiple`, `searchable`, `page_size`, `row_actions` (§5.2) | the row key, or array |
| `tree` | `nodes[]` of `{id, label?, icon?, badge?, expanded?, children?[]}`, `multiple`, `searchable` | the node id, or (with `multiple`) the checked ids |
| `diff` | `patch`, `select` (`hunks` \| `files`), `default` (`all` \| `none` \| ids) | the chosen ids: `"path@n"` per hunk, or `"path"` (§5.3) |
| `filepick` | `filter`, `initial_dir`, `path_format` | string |
| `folderpick` | `initial_dir`, `path_format` | string |
| `flags` | `options[]` of `{name,label,description?,default}` | `{name: bool, ...}` |

`options` accepts plain strings or `{value, label?, description?, icon?}` (`icon` shows on `cards`). A field
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

### 5.2 Tables: cell formats and row actions

Each column may say how its cells look with `format`: `mono` (ids, hashes, paths), `muted`, `number` (right-aligned),
`badge` (the cell value is `{"text": "merged", "tone": "success"}` or a plain string) and `copy` (mono, with a copy icon
on hover). Tones: `neutral` `info` `success` `warning` `error` `accent`. Unknown formats show as text.

**Row actions** are chips tied to a row: icons on the right of each row (they show on hover, on the selected row and
while running; also in the row's right-click menu, which the keyboard reaches with the Menu key). Up to 3 icons, the
rest go to "…".

```json
{"name":"branch","type":"table","row_key":"name",
 "columns":[{"key":"name","label":"Branch","format":"mono"},{"key":"state","label":"State","format":"badge"}],
 "rows":[{"name":"main","state":{"text":"default","tone":"info"},"_actions":["sync"]},
         {"name":"feat/login","state":"merged"}],
 "row_actions":[{"id":"sync","label":"Sync","icon":"refresh-cw"},
                {"id":"delete","label":"Delete","icon":"trash-2","danger":true,
                 "confirm":{"text":"The local branch is gone for good.","confirm_label":"Delete","cancel_label":"Keep"}}]}
```

- A row action has the keys of a chip (`id`, `label`, `description`, `icon`, `danger`, `confirm`) — `requires` does not
  apply, the row is the requirement.
- `_actions` in a row lists which actions that row allows (above: `main` cannot be deleted). Without it, all of them.
- With `confirm`, the row itself turns into "Delete feat/login? … [Keep] [Delete]" — focus on Keep, Enter never
  confirms by accident. Nothing is sent before the confirmation.
- Running: the icon becomes a spinner and the other actions wait (one at a time, chips included). Kai sends
  `{"type":"chip","id":"pick","chip":"delete","field":"branch","row":"feat/login","values":{…}}` (`table` instead of
  `field` for a table *block*); answer with `chip_result` exactly as for a chip. On success the row flashes green; a
  `text` shows in the box under the fields. To change the table (remove the deleted row), send a `patch` without `seq`.
- On a table **block** the actions need the table `id`, and they only work while a prompt is open (that is when the
  program reads Kai's messages).

### 5.3 Diffs and comparing files

`diff` shows changes: send the output of `git diff` / `diff -u` as `patch` (many files, renames, new/deleted and binary
files), or two whole texts as `left` and `right` and Kai compares them.

```json
{"kip":1,"type":"diff","id":"wc","title":"Uncommitted changes","patch":"diff --git a/x b/x\n..."}
{"kip":1,"type":"diff","title":"staging × production","left":{"name":"staging.json","text":"…"},
 "right":{"name":"production.json","text":"…"},"language":"json","context":3}
```

- Changed words inside a changed line are highlighted; JSON, YAML, XML and Markdown get syntax colors (`language`, or
  the file extension).
- **Unified or side by side** is the user's choice, kept per command (with the remembered answers). `view` is only the
  default for a command the user never switched; two texts default to side by side.
- Two texts: equal stretches longer than `2 × context` (default 3) fold into "⋯ 31 unchanged lines" (click to open);
  `context: -1` shows everything; `ignore_whitespace` compares the lines without spaces. ↑ ↓ (or F7 / Shift+F7) walk the
  changes.
- More than `collapse_after` files (default 3) start collapsed. Right-click copies a line or a path.
- Kai never reads files for a diff: your program (or the helper, §6) sends the text. That keeps WSL, containers and SSH
  working and keeps the protocol from reading arbitrary files.

**As a field** (`type: "diff"`), the same view gets checkboxes: per hunk (`select: "hunks"`) or per file (`"files"`).
The answer is the list of ids that stayed checked — `"src/auth/session.ts@0"` is the first hunk of that file — and your
program builds the patch (for a partial commit: `git apply --cached` with the chosen hunks). Everything starts checked
(`default: "none"` starts empty). Space toggles the row under the cursor, Enter opens or closes it. A diff field is never
remembered between runs.

### 5.4 Result blocks: `kv`, `stats`, `code`

- **`kv`**: `items[]` of `{key, value, format?, copy?}`; `format`: `mono`, `muted`, `badge` (value `{text, tone}`) or
  `link` (http/https only — anything else shows as text). `copy: true` shows a copy button on hover.
- **`stats`**: `items[]` of `{label, value, detail?, tone?}` — big numbers, up to 4 per row; `tone` colors the dot of the
  detail.
- **`code`**: `text`, `title?`, `language?`, `highlight?` (line numbers), `max_lines?` (longer code collapses with "Show
  all (42 lines)"), `line_numbers?` (default `true`). A copy button copies the whole text.

All three stay on the result screen, like tables and Markdown.

### 5.5 Step output and several bars

`step` with `log` appends lines to that item's output (the last 200 are kept). An item with output gets a chevron;
the output is collapsed by default and opens by itself when the step fails. `progress` with `id` gives one bar per id
(stacked, compact; a bar at 100 turns green) — without `id` it is the single bar of before.

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
| `progress` | `<0-100\|null> [label] [--no-cancel] [--id ID]` |
| `steps` | `--id ID [--title T] --item ID [label] [--state S] [--detail D] …` |
| `step` | `<steps-id> <item-id> [<state> [detail]] [--log TEXT]` (the state may be left out with `--log`) |
| `table` | `[--id ID] [--title T] --column key:label[:format] … --rows-json '[…]' [--row-key K] [--row-action …]` |
| `diff` | `[--id ID] [--title T] --patch TEXT\|- \| --patch-file PATH\|- \| --left-file A --right-file B` (or `--left-text`/`--right-text`), `[--left-name N] [--right-name N] [--language L] [--view unified\|split] [--context N] [--ignore-whitespace] [--collapse-after N]` |
| `kv` | `[--id ID] [--title T] --item KEY VALUE [--format F] [--tone T] [--copy] …` |
| `stats` | `[--id ID] --stat LABEL VALUE [--detail D] [--tone T] …` |
| `code` | `[--id ID] [--title T] [--language L] TEXT… \| --file PATH\|- [--highlight 4,5] [--max-lines N] [--no-line-numbers]` |
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
`--column key:label[:format]`, `--row-key K`, `--rows-json '[…]'`, `--search`, `--no-search`, `--page-size N`,
`--style cards|segmented` and `--option-icon NAME` (select; the icon goes to the last option), `--max-length N`
(text/textarea), `--nodes-json '[…]'` (tree), `--patch TEXT|-`, `--patch-file PATH|-` and `--select files|hunks` (diff).
Before the first `--field`, `--description` and `--no-remember` belong to
the prompt.

**Chip options** (`prompt` and `patch`): `--chip ID [label]` starts a chip; `--chip-description T`,
`--chip-icon NAME`, `--chip-danger`, `--chip-requires f1,f2`, `--chip-confirm`, `--chip-confirm-title T`,
`--chip-confirm-text T`, `--chip-confirm-label L` and `--chip-cancel-label L` apply to the last chip.

**Row actions** (a `table` field in `prompt`/`patch`, or the `table` verb): `--row-action ID [label]` starts one;
`--row-action-icon NAME`, `--row-action-danger`, `--row-action-description T`, `--row-action-confirm`,
`--row-action-confirm-title T`, `--row-action-confirm-text T`, `--row-action-confirm-label L` and
`--row-action-cancel-label L` apply to the last one.

**Reading text.** `--patch -`, `--patch-file -` and `--file -` read stdin; `--patch-file`, `--left-file`, `--right-file`
and `--file` read files where the script runs. The final newline is dropped and anything above 512 KiB is cut at the
last whole line (`truncated` is set). Read stdin only from a pipe — `git diff | kai kip diff --patch -` — never on its
own: a KIP program's stdin is Kai's channel.

```bash
git diff --staged | kai kip diff --title "Staged" --patch -
kai kip diff --left-file config/staging.json --right-file config/production.json --view split
kai kip kv --item Version 1.4.0 --format mono --copy --item Channel stable --tone success
```

**`get` paths** are dot-separated (`values.opts.dry`, `values.tags.0`).
Strings print raw, numbers and booleans as JSON literals, arrays one item
per line, objects as compact JSON. A missing path prints nothing and exits 1.

Invalid usage writes a message to **stderr**, nothing to stdout, and exits
2 — stdout is the protocol channel and never gets garbage. Output always
ends in `\n`.

Any language works the same way without the helper: print the JSON lines
yourself (see `sample/kip/raw-protocol.sh`).

## 6.1 Writing a KIP program in Python, Node or PHP

Set the command's **Language** to Python, Node or PHP (see [commands.md](commands.md)) and turn **KIP** on:
the text of the command is the program itself — no file to ship, no JSON to print by hand. Kai injects a
small `kip` module (Python: `import kip`; Node: the global `kip`, or `require('kip')`; PHP: the class `Kip`) and it
sends `hello` before your first message, so a whole wizard is a few lines:

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

```php
$values = Kip::prompt([
    ['name' => 'env', 'type' => 'select', 'label' => 'Environment', 'options' => ['dev', 'prod'], 'required' => true],
], ['id' => 'where', 'title' => 'Where to?']);

Kip::progress(50, 'Deploying…');
Kip::done('Deployed to ' . $values['env']);
```

| Python | Node | PHP | Does |
|---|---|---|---|
| `kip.prompt(fields, id=, title=, on_change=, on_chip=, validate=, ...)` | `await kip.prompt(fields, {id, title, onChange, onChip, validate, ...})` | `Kip::prompt($fields, ['id' => , 'title' => , 'onChange' => , 'onChip' => , 'validate' => , ...])` | Shows the form, returns the `values` dict/object/array. Other options are the prompt keys of §3 (`description`, `submit_label`, `back`, `cancellable`, `remember`, `chips`). |
| `kip.confirm(text, id=, danger=, ...)` | `await kip.confirm(text, {id, danger, ...})` | `Kip::confirm($text, ['id' => , 'danger' => , ...])` | Yes/no → `True`/`False`. |
| `kip.message(text, level="info")`, `kip.markdown(text)` | `kip.message(text, level)`, `kip.markdown(text)` | `Kip::message($text, $level)`, `Kip::markdown($text)` | Status card / Markdown. |
| `kip.progress(value, label)` | `kip.progress(value, label)` | `Kip::progress($value, $label)` | `value` 0–100 or `None`/`null`. |
| `kip.steps(id, items)`, `kip.step(steps_id, id, state, detail, log=)` | `kip.steps(id, items)`, `kip.step(stepsId, id, state, detail, log)` | `Kip::steps($id, $items)`, `Kip::step($stepsId, $id, $state, $detail, $log)` | Checklist; `state` may be `None`/`undefined`/`null` to only append `log`. |
| `kip.table(columns, rows, id=, title=, row_key=, row_actions=)` | `kip.table(columns, rows, {id, title, row_key, row_actions})` | `Kip::table($columns, $rows, ['id' => , 'title' => , ...])` | Read-only table (§5.2). |
| `kip.diff(patch=)` or `kip.diff(left=, right=, ...)` | `kip.diff({patch})` or `kip.diff({left, right, ...})` | `Kip::diff(['patch' => ])` or `Kip::diff(['left' => , 'right' => , ...])` | A diff (§5.3). `left`/`right`: a text or `{name, text}`. |
| `kip.kv(items)`, `kip.stats(items)`, `kip.code(text, language=, ...)` | `kip.kv(items)`, `kip.stats(items)`, `kip.code(text, {...})` | `Kip::kv($items)`, `Kip::stats($items)`, `Kip::code($text, [...])` | Result blocks (§5.4). |
| `kip.notify(...)`, `kip.set_env(name, value)` | `kip.notify(...)`, `kip.setEnv(name, value)` | `Kip::notify(...)`, `Kip::setEnv($name, $value)` | Notification / exported variable (§7). |
| `kip.done(title=, text=, level=, actions=)` | `kip.done({title, text, level, actions})` | `Kip::done($title, $text, $level, $actions)` or `Kip::done([...])` | Result screen. |
| `kip.send(type, **fields)`, `kip.log(...)` | `kip.send(type, fields)`, `kip.log(...)` | `Kip::send($type, $fields)`, `Kip::log(...)` | Any message / a line for **Details → Log**. |

- `validate(values)` returns `{field: message}` to keep the form open (Kai gets an `invalid`); `on_change(field,
  values)` returns the fields to patch (the `seq` is echoed for you); `on_chip(chip, values)` returns a text or
  `{state, title, text}` (an exception becomes an `error` result); `on_row_action(action, row, values)` /
  `onRowAction` does the same for a row action (§5.2) and gets the row key.
- **Cancel** exits the process with code 130. **Back** raises `kip.Back` (Python) / rejects with `kip.Back` (Node) /
  throws `KipBack` (PHP).
- Node code runs inside an async function, so top-level `await` works.
- PHP: the callbacks are closures in the options array (`onChange`, `onChip`, `validate`) and `validate` returns
  `[field => message]`. PHP cannot tell `[]` from `{}`: it sends `[]`, so use `(object)[]` where the protocol wants an
  empty object. The helper works from PHP 7.4.
- Output is unbuffered and UTF-8, so there is no flush to forget. Variables and parameters come from
  `os.environ` / `process.env` / `getenv()` (`{{VAR}}` is not replaced in Python/Node/PHP code).
- Python and Node commands also get the **`kai` module** to talk to the Kai app (notifications, environments,
  processes, running other commands) — see [languages.md](languages.md). PHP has no `kai` module.
- With KIP **off**, `import kip` / `Kip` still exist but any use of them raises an error that says to turn KIP on for
  the command.
- **Your own script files** find the same modules too: a Native command with KIP on that runs `python3 tool.py`,
  `node tool.js` or `php tool.php` needs no bootstrap — see "Scripts you run as files" in
  [languages.md](languages.md).

## 6.2 Writing a KIP program in the shell

Turn **KIP** on in a **Native** command that runs in a POSIX shell (Linux, macOS, WSL and other POSIX profiles) and Kai
defines a `kip` function before the text runs, with the **same verbs and options as `kai kip`** (§6) — but with no
`kai` binary involved, so it also works inside WSL, containers and SSH sessions. Write `kip ...` instead of
`kai kip ...`:

```bash
kip prompt --id where --title "Where to?" --field select env Environment --options dev,staging,prod --required
kip recv                                   # waits for Kai's answer, leaves it in $KIP_MSG
target=$(kip get "$KIP_MSG" values.env)
kip progress 50 "Deploying..."
kip done --title "Deployed to $target"
```

- It is a normal native command: `{{VAR}}` is replaced as always. The shell is bash (sh where there is no bash).
- Verbs: `hello prompt confirm patch invalid message markdown progress steps step table notify set-env done
  chip-result diff kv stats code`, plus `raw` and `get` (§6). `hello` is sent for you if the script does not.
- `git diff | kip diff --patch -` works: the `kip` of a pipe runs in a subshell, which sends one more `hello` without a
  title — Kai ignores it.
- `kip recv` reads the next line from Kai into `$KIP_MSG`; **Cancel** (or a closed stdin) exits with code 130.
- `kip get '<json>' <path>` extracts a value (`values.env`, `items.0.id`); it exits 2 when the path is missing.

**Limits:**

- The helper is POSIX `sh` + `awk` (mawk, gawk, busybox awk all work). `awk` must exist where the command runs, and
  the inline form also needs `base64` and `gzip`. There is no `jq` or Python involved (your own program may use them).
- It keeps a few `_kip_*` variables: don't reuse those names. `kip recv` uses `read -r`, so it must be used in the
  main script, not in a subshell (`$( ... )`) — the answer would be lost with the subshell.
- **`kip get` / `kip raw`**: numbers in exponent form are printed as written, NUL bytes are dropped, and `raw` only
  checks that the text is a JSON object with a `kip` key and a known `type`.
- Messages and usage errors of the helper are in English only.
- **Windows**: without a POSIX profile (plain cmd.exe / PowerShell) there is no `kip` function: use a WSL profile, or
  write the program in Python, Node or PHP. A command too long for the command line is written by Kai to a file of its
  own and loaded from there (see [Languages](languages.md)) — in the shell as well as in Python, Node and PHP.

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
- **Details** (collapsed by default, on the left of the buttons row): *Log* (stderr and non-protocol
  stdout) and *Protocol* (every message in both directions, `←` from the
  program, `→` from Kai, secrets masked).
- Failure, cancellation, unsupported and protocol-error states are full
  cards with a **Run again** button; all but cancellation open the details.
- With `kip_window`, the view lives in its own window and the panel shows
  "Open elsewhere"; closing the window brings it back.
- **Pin as panel** (right click the command): the KIP command becomes a permanent [panel](layout.md#pinning-a-kip-command) of the window, with a **Run** card until it runs. The Output shows the same card while the pinned panel exists.
- The output panel's own tab bar is hidden for KIP commands; the small pop-out icon in the top-right of the view opens it in its own window.
- **Chips** sit under the fields; their confirmation, progress and result share one box under them.

## 9. Command line

- `kai -g <command>` runs a KIP command like a click: the app comes to the
  front on the KIP view. With `-w` (or `kip_window: true`) the view opens in
  its own window and the **main window is left alone** — it is not shown or
  raised. The same goes for `kai run` and `kai.run()` on a `kip_window`
  command. The client
  prints the log lines and returns the real exit code.
- Local mode (`kai <path>` against a `kai.yml`) **refuses** KIP commands
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

## 11. Settings and the creation manifesto

**Settings → KIP** holds the global preferences: how long to wait for the program's first message
(default 10 s), for the `patch` that answers a field update (10 s) and after **Cancel** (3 s);
whether to remember the last answers (and a button to forget them all); and whether the Details
drawer opens by itself when a command fails; and **Own-window size** — how the view opens when it runs
in its own window: *follow the general window setting* (Settings → Appearance → Open as), *normal window*,
*maximized* or *fullscreen* (it decides only for KIP views, never for the main window). Whether a given command uses KIP is set in *its* editor
(its **KIP** tab), and only command-type commands (not HTTP) have that option.

**Help → Creation manifesto** shows and copies, with one click, the two guides meant to be handed to an AI assistant:
the [Kai manifesto](../assets/manifesto/kai-manifesto.md) (for `kai.yml`, with the icon list) and the
[KIP manifesto](../assets/manifesto/kip-manifesto.md), which teaches the model to write a KIP program
(protocol, templates in bash and Python, pitfalls, testing and a checklist).

## 12. Security

A KIP program cannot make Kai run anything: `open_url` accepts only
http/https, `set_env` is allowlisted, Markdown links open externally only
for http/https and remote images are never fetched, and secret values are
redacted in the inspector, history and logs.

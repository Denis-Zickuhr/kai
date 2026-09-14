# The KIP manifesto — a guide to writing programs that talk to Kai

> Self-contained document meant to **feed an AI model** (or a human) that has to
> write a program speaking **KIP — the Kai Interface Protocol**. Give this file to
> the model together with what the program must do ("a wizard that deploys my app",
> "a browser for my DynamoDB tables"…) and it has everything it needs.
>
> This guide is about **writing the program** (the thing that prints JSON lines) and
> **registering it in a `kai.yml`** (§12; the YAML rules are in the Kai manifesto §1). It has
> nothing to do with developing Kai itself. Every key, type and rule below is what Kai
> accepts today (protocol version `1`). Complete, runnable examples are in §13.5.
>
> Fence legend: ` ```jsonl ` = one protocol line; ` ```yaml file ` = a complete importable
> `kai.yml`; ` ```yaml model <name> ` = the exhaustive keys of a structure (a reference, not a file).

---

## 1. What KIP is, and when to use it

KIP turns a command's output panel into a small **native app**. Instead of printing
text for a terminal, the program prints one JSON object per line; Kai draws forms,
confirmations, lists, tables, progress bars, checklists and a result card, and sends
the user's answers back on stdin.

Use KIP when a command is **interactive and structured**: wizards (deploy, restore,
scaffold, onboarding), logins, "pick one of N things then act", lookups with a result
table, maintenance tools with destructive actions that deserve a confirmation.

Do **not** use it for: plain logs/streams (just print text), full-screen terminal apps
(vim, htop — use the interactive terminal option), or anything that must also run in a
non-Kai terminal without changes (see §14 on keeping a plain mode).

The program can be written in **any language**. The only requirements: print protocol
lines on **stdout**, read answer lines on **stdin**, flush after every line.

---

## 2. The transport — five rules that prevent 90% of the bugs

1. **JSON Lines**: exactly one JSON object per line, UTF-8, ending in `\n`. Never
   pretty-print a message over several lines. (`\r\n` is tolerated.)
2. **stdout is the protocol channel**, stdin carries Kai's answers, **stderr is a free
   log** (shown in the "Details → Log" drawer, never parsed). Print debugging to
   stderr. A stdout line that is not a JSON object with a `"kip"` key is not an error:
   it goes to the log, so wrappers that echo a banner still work.
3. **Flush after every line.** Python: `print(..., flush=True)`; Node: `process.stdout.write(line+"\n")`;
   C: `fflush(stdout)`; Go: use `bufio.Writer` and `Flush()`. A buffered line is a
   program that "hangs" from Kai's point of view.
4. **Every message has the envelope** `{"kip":1,"type":"<type>", ...}`.
5. **Limits** (extra is truncated and logged): a line ≤ 1 MiB, ≤ 100 fields per prompt,
   ≤ 5000 options per field, ≤ 1000 table rows, ≤ 200 checklist items, ≤ 24 chips,
   `page_size` ≤ 200, a text value ≤ 64 KiB.

Kai never interpolates `{{VAR}}` inside protocol messages — what you print is what is
shown. Program texts are shown as-is (Kai does not translate them); the environment
variable `KIP_LOCALE` (`en` or `pt`) tells you Kai's language if you want to localize.

---

## 3. The handshake

The **first** protocol message must be `hello`:

```jsonl
{"kip":1,"type":"hello","title":"Deploy","version":"1.4.2"}
```

`title` (optional) names the app on screen; `version` (optional) is your own version,
shown discreetly. If the process exits before a valid `hello`, or no `hello` arrives in
the configured time (default 10 s, Settings → KIP), Kai shows **"This command doesn't
support KIP"** with the log open. A first message that is not `hello`, or a `hello`
with `"kip"` greater than the version Kai speaks, is a protocol error.

---

## 4. Messages

### 4.1 Program → Kai

| `type` | Keys | What it does |
|---|---|---|
| `hello` | `title?`, `version?` | Handshake (§3). |
| `prompt` | `id`, `title?`, `description?`, `submit_label?`, `back?`, `cancellable?`, `remember?`, `fields[]`, `chips[]?` | A form. Kai waits for a `response`. |
| `confirm` | `id`, `text`, `title?`, `danger?`, `confirm_label?`, `cancel_label?`, `back?`, `cancellable?` | Yes/no screen. |
| `patch` | `id`, `seq`, `fields[]?`, `remove[]?`, `chips[]?` | Updates the **open** prompt `id` (§7). |
| `invalid` | `id`, `errors{field: text}`, `message?` | Rejects an answer; the same prompt stays open with the errors shown (§6). |
| `message` | `level` (`info` `success` `warning` `error`), `text` | A status card on the current screen. |
| `markdown` | `text` | Formatted text (headings, lists, code, http/https links; images are never loaded). |
| `progress` | `value` (0–100 or `null`), `label?`, `cancellable?` | A progress bar; the next `progress` updates the same bar. `null` = indeterminate. |
| `steps` | `id`, `title?`, `items[]` of `{id, label, state?, detail?}` | A checklist. |
| `step` | `steps`, `id`, `state`, `detail?` | Updates one checklist item. States: `pending` `running` `success` `error` `skipped`. |
| `table` | `id?`, `title?`, `columns[]` of `{key, label}`, `rows[]` of objects | A read-only table (users can copy a cell). Same `id` ⇒ replaces the previous one in place. |
| `notify` | `title`, `text?`, `level?` | A system notification (respects the user's notification settings). |
| `set_env` | `name`, `value` | Exports a variable to Kai (§11). |
| `chip_result` | `chip`, `state?`, `title?`, `text?`, `id?` | Progress/outcome of a running chip (§9). |
| `done` | `title?`, `text?`, `level?`, `actions[]?` | The result screen. **Exit right after.** |

Unknown `type`s are ignored (and logged). `id`s are strings you choose. A `response`/
`patch`/`invalid` for an id that is not the open screen is ignored. Sending a new
`prompt`/`confirm` while one is open replaces it.

### 4.2 Kai → program (one JSON line each, on stdin)

```jsonl
{"kip":1,"type":"response","id":"target","values":{"env":"prod","opts":{"dry":false}}}
{"kip":1,"type":"change","id":"nav","seq":3,"field":"context","values":{"context":"eu","namespace":"default"}}
{"kip":1,"type":"chip","id":"pick","chip":"ctx","values":{"branch":"feat/login"}}
{"kip":1,"type":"back","id":"sure"}
{"kip":1,"type":"cancel"}
```

| `type` | Keys | When |
|---|---|---|
| `response` | `id`, `values{}` | The user submitted prompt/confirm `id`. For a `confirm`: `values.confirmed` is `true`/`false` (declining is an answer, not a cancel). |
| `change` | `id`, `seq`, `field`, `values{}` | A field with `watch:true` changed (§7). |
| `chip` | `id`, `chip`, `values{}` | The user ran a chip (§9). The prompt stays open. |
| `back` | `id` | The user pressed Back on a prompt/confirm that allowed it. |
| `cancel` | — | The user pressed Cancel. Exit promptly. |

The key order on the wire is alphabetical — never depend on it. Parse JSON properly in
your language; in bash a tiny `sed` is enough for flat values (see §13).

---

## 5. Fields

Common keys: `name` (required, unique in the prompt), `label`, `description`, `required`
(default `false`), `default`, `placeholder`, `group` (fields with the same group go in one
collapsible section), `watch` (§7), `remember` (§6).

| `type` | Extra keys | Value in `response.values.<name>` |
|---|---|---|
| `text` | — | string |
| `secret` | — (masked, **never remembered**, hidden in the inspector) | string |
| `textarea` | — | string |
| `number` | `min`, `max`, `step`, `decimals` | JSON number, or `null` when left empty |
| `date` | `mode` (`date` `time` `datetime`), `range` | ISO string (`yyyy-MM-dd`, `HH:mm:ss`, `yyyy-MM-ddTHH:mm:ss`); with `range`: `{"start","end"}` |
| `select` | `options[]` | string (`""` if none chosen) |
| `list` | `options[]`, `multiple`, `searchable`, `page_size` | string, or an array of strings with `multiple` |
| `table` | `columns[]`, `rows[]`, `row_key` (default `"id"`), `multiple`, `searchable`, `page_size` | the **row key** (string), or an array with `multiple` |
| `filepick` | `filter` (Qt name filter, e.g. `"Images (*.png *.jpg);;All (*)"`), `initial_dir`, `path_format` | string |
| `folderpick` | `initial_dir`, `path_format` | string |
| `flags` | `options[]` of `{name, label, description?, default}` | an object `{name: bool}` — **every flag is present** |

* `options[]` accepts plain strings (value = label) or `{value, label?, description?}`.
* An unanswered field is sent empty: `""`, `[]`, `false` per flag, `null` for number/date.
* `required` is checked by Kai (Submit stays disabled). Anything else is **your** job: reply
  with `invalid` (§6).
* **Long lists:** `searchable:true` always shows a filter box (automatic only above 8 items;
  `false` hides it). `page_size:N` shows N items per page with a "Page 2 of 5" bar. Both work on
  the **filtered** result. This is client-side over what you sent — for data that does not fit
  in the limits, page on your side with chips or `patch` (§9, §10).
* **Paths and WSL/Windows:** when Kai runs on Windows and your program in WSL, put
  `"path_format":"posix"` on `filepick`/`folderpick` so you receive `/mnt/c/...`; use the same
  `"path_format":"posix"` on `reveal` actions (§8) so Kai can open the result. `native` (default)
  leaves the path untouched.

```jsonl
{"kip":1,"type":"prompt","id":"target","title":"Where to deploy?","submit_label":"Next","fields":[{"name":"env","type":"select","label":"Environment","required":true,"default":"staging","options":[{"value":"dev","label":"Development"},{"value":"staging","label":"Staging","description":"Mirror of production"},{"value":"prod","label":"Production"}]},{"name":"opts","type":"flags","label":"Options","options":[{"name":"dry","label":"Dry run","default":false},{"name":"notify","label":"Notify the team","default":true}]}]}
```

---

## 6. Prompt behaviour you must respect

* **Submit.** The user fills the form and presses the submit button (`submit_label`, default
  "Continue"). You get one `response`. After submitting, the form is locked until the next
  screen, an `invalid`, or your exit.
* **Validation = `invalid`, and then WAIT.** To reject an answer print
  `{"kip":1,"type":"invalid","id":"<same id>","errors":{"name":"Only lowercase letters"},"message":"Fix the highlighted fields"}`
  and **read the next `response`** — the same prompt stays open with the errors shown and the
  user's input intact. **Do not print the prompt again**: a new `prompt` with the same id
  replaces the screen and wipes the errors the user is about to read. This is the most common
  mistake.
* **Back.** `"back":true` on a prompt/confirm shows a Back button. When pressed you get
  `back` and decide which screen comes next (usually: print the previous prompt again).
* **Cancel.** Shown unless `"cancellable":false` (on the open prompt or the latest progress).
  When you get `cancel`, clean up and exit promptly (exit code `130` is conventional); after a
  grace period (default 3 s) Kai stops the process.
* **Remembered answers.** By default Kai pre-fills each field with what the user submitted last
  time for the same command + prompt `id` + field `name` (never `secret`). So **keep prompt ids
  and field names stable**. Opt out with `"remember":false` on the prompt or the field. A remembered
  value that is no longer valid (option gone, out of range) falls back to `default`.
* **Confirm.** `{"type":"confirm","id","text",...}` shows two buttons. With `"danger":true` the
  confirm button is red and focus starts on the *decline* button, so Enter never confirms a
  destructive action by accident. Always use `danger` for deletes/overwrites.

```jsonl
{"kip":1,"type":"confirm","id":"sure","title":"Overwrite the database?","text":"This replaces the CURRENT database with backup b2. Anything newer is lost.","danger":true,"confirm_label":"Restore","cancel_label":"Keep current data","back":true}
{"kip":1,"type":"invalid","id":"project","message":"That name can't be used","errors":{"name":"Start with a letter; then lowercase letters, digits or dashes only"}}
```

**The screen model.** A *screen* is the open prompt/confirm plus the display blocks
(`message`, `markdown`, `progress`, `steps`, `table`) printed since the previous screen was
answered. Blocks printed while no prompt is open form a "running" screen. Answering a prompt
clears its blocks. A `progress` block disappears when `done` arrives; messages, Markdown,
tables and checklists stay on the result screen.

---

## 7. Dependent fields: `watch` + `patch`

A field with `"watch":true` makes Kai send a `change` whenever its value changes (text is
debounced 300 ms). **Every `change` must be answered with a `patch` that echoes its `seq`**
(an empty one is fine). Each entry of `fields` *replaces* the field with the same `name` (or is
appended if new); `remove` drops fields by name. While a change is pending, Submit is disabled
and the form shows "Updating…". A `patch` older than the latest `change` is ignored, and if no
`patch` arrives in time (default 10 s) the form unlocks again with a warning. A replaced field
keeps the user's value when it is still valid, otherwise it resets to its `default`.

```jsonl
{"kip":1,"type":"prompt","id":"nav","title":"Pick a pod","fields":[{"name":"context","type":"select","watch":true,"default":"prod-eu","options":["prod-eu","staging"]},{"name":"namespace","type":"select","options":["default","payments"]},{"name":"pod","type":"list","options":["api-1","api-2"]}]}
{"kip":1,"type":"patch","id":"nav","seq":1,"fields":[{"name":"namespace","type":"select","default":"default","options":["default","sandbox"]},{"name":"pod","type":"list","options":["api-dev-1"]}]}
```

**Spontaneous patch.** A `patch` **without `seq`** is not an answer to a change: it is always applied and it does
not release a pending change. Use it for what the program decides on its own — a chip that repaints a table or
a list (page change, delete, refresh), or a refresh after an action. (An explicit `"seq":0` sent after a
`change` is *stale* and ignored: leave `seq` out.) In the Python/Node modules: `kip.patch(id, fields=…, chips=…)`.

```jsonl
{"kip":1,"type":"patch","id":"nav","fields":[{"name":"pod","type":"list","options":["api-1"]}],"chips":[]}
```

---

## 8. Display blocks, checklists, results

```jsonl
{"kip":1,"type":"message","level":"warning","text":"The VPN request failed; retry it later."}
{"kip":1,"type":"markdown","text":"## Welcome\nThis runs on its own — **watch the steps tick off**."}
{"kip":1,"type":"progress","value":40,"label":"Uploading… 40%"}
{"kip":1,"type":"progress","value":null,"label":"Talking to the server…"}
{"kip":1,"type":"steps","id":"setup","title":"Setting up","items":[{"id":"git","label":"Configure git"},{"id":"repos","label":"Clone the repositories"}]}
{"kip":1,"type":"step","steps":"setup","id":"git","state":"success","detail":"name and email"}
{"kip":1,"type":"table","id":"results","title":"3 customers","columns":[{"key":"id","label":"ID"},{"key":"name","label":"Name"}],"rows":[{"id":"c-1","name":"Acme"}]}
{"kip":1,"type":"notify","title":"Done","text":"5 of 6 steps finished","level":"warning"}
```

**`done`** ends the run and shows the result card (icon from `level`, default `success`):

```jsonl
{"kip":1,"type":"done","title":"Deployed to prod","text":"Version 1.4.2 is live.","level":"success","actions":[{"type":"open_url","label":"Open the site","url":"https://example.com"},{"type":"reveal","label":"Reveal folder","path":"/tmp/out","path_format":"posix"},{"type":"copy","label":"Copy release id","value":"rel-1042"}]}
```

| Action `type` | Keys | Behaviour |
|---|---|---|
| `open_url` | `label`, `url` | Opens the browser. **Only http/https** — anything else is rejected. |
| `reveal` | `label`, `path`, `path_format?` | Opens the file manager at the path (a file → its folder). The button is disabled if the path does not exist. |
| `copy` | `label`, `value` | Copies to the clipboard. |

"Run again" is always offered by Kai. **Exit right after `done`.** The final status comes from
the **exit code**: `0` = success, non-zero = failure *even after a `done`*. Exiting while a prompt
is open is a failure ("the program exited while waiting for input"). Exiting `0` without `done`
shows a generic "Finished" card.

---

## 9. Chips — ephemeral actions that do not leave the step

A **chip** is a small button under the fields of a `prompt`, for actions that are *inert and
informative* ("show the context of this branch", "sync", "delete"). Clicking one does **not**
answer the prompt: Kai sends `chip`, your program works and reports with `chip_result`, and the
outcome appears in a box under the chips. The step stays open and an `error` never ends the session.

```jsonl
{"kip":1,"type":"prompt","id":"pick","title":"Pick a branch","fields":[{"name":"branch","type":"list","required":true,"searchable":true,"page_size":5,"options":["main","feat/login","fix/cart"]}],"chips":[{"id":"ctx","label":"Context","icon":"info","description":"Ticket and PR status","requires":["branch"]},{"id":"delete","label":"Delete branch","icon":"trash-2","danger":true,"requires":["branch"],"confirm":{"title":"Delete this branch?","text":"This cannot be undone.","confirm_label":"Delete","cancel_label":"Keep"}}]}
{"kip":1,"type":"chip_result","chip":"ctx","state":"running","text":"Looking up the ticket…"}
{"kip":1,"type":"chip_result","chip":"ctx","state":"success","title":"Context · feat/login","text":"**Ticket** ABC-123\n\n- PR #482: open"}
{"kip":1,"type":"chip_result","chip":"delete","state":"error","title":"Not deleted","text":"`main` is the default branch."}
```

| Chip key | Meaning |
|---|---|
| `id` | required, unique in the prompt |
| `label` (default: the id), `description` (tooltip), `icon` (a name from Kai's icon pool, optional) | looks |
| `danger` | destructive look; the confirmation button turns red |
| `confirm` | `true`, or `{title?, text?, confirm_label?, cancel_label?}` — Kai asks **inside the box** before sending anything |
| `requires` | field names that must be filled before the chip is enabled |

* When the chip runs Kai sends `{"type":"chip","id":"<prompt>","chip":"<chip>","values":{…current values…}}`.
* Answer with as many `chip_result` messages as you like: `state` is `running` (default), then
  `success` or `error`. `text` is Markdown; `title` defaults to the chip label; `id` is optional.
* One chip runs at a time; the finished box stays until the user closes it. A `chip_result` that
  matches no running chip is ignored.
* `patch` can replace the whole chip set (`"chips":[…]`; `[]` removes all).
* **Never do the destructive work before a `confirm`** — put the question in the chip's `confirm`
  and let Kai ask; you only receive `chip` after the user agreed.
* Chips are for actions *about the current selection*. For things that move the wizard forward, use
  the normal submit.

---

## 10. Pagination and big data

**Recipe — browsing a table (DynamoDB, SQL, an API) with create/edit/duplicate/delete.** It needs only the
primitives above; a runnable version (fake in-memory data) is `sample/kip/table-browser.py` in the Kai repository:
1. Screen 1: a `list` field (`searchable`) with the table names, submit *Open*.
2. Screen 2, one `prompt` with `back:true`: a `text` field `filter` with `watch:true` (on `change`, reset to page 1,
   reload, answer with a `patch` that replaces the table field) and a `table` field (`row_key`, `multiple:true`,
   `columns`, the page's `rows`, a `description` such as "Items 9–16 · more available").
3. Chips: *Previous* / *Next* / *Refresh* (the handler keeps the cursor, then sends a **spontaneous patch** with the
   new rows and the new chip set — drop *Previous* on page 1 and *Next* on the last page — and answers `chip_result`);
   *Delete* with `confirm` + `danger` and `requires:["rows"]`.
4. *New* / *Edit* / *Duplicate* leave the table: print a second `prompt` (a `textarea` with the item as JSON, validated
   with `invalid`, `back:true`). On save print the table prompt again — it replaces the editor.
5. A composite key is one string for `row_key` (e.g. `"pk|sk"`). Cell values are shown as text; keep long values short.

KIP sends everything you print, so keep the screen reasonable: for thousands of rows page **on
your side**. A proven pattern (DynamoDB scan, API listings, log browsers): print a `table` block
with a fixed `id` for the current page and offer chips **Previous / Next / Filter**; each chip click
re-emits the `table` with the same `id` (it replaces the previous one in place) and a
`chip_result` saying "Page 3 · 100 items". Keep the cursor/offset in your program.

Selecting rows to act on: a `table` *field* with `"multiple":true` returns the row keys; pair it
with chips that `require` it (bulk delete with `confirm`+`danger`).

---

## 11. Talking to the rest of Kai

* **`set_env`** — `{"kip":1,"type":"set_env","name":"API_TOKEN","value":"…"}` exports a dynamic
  variable that other commands can use as `{{API_TOKEN}}`. Only names listed in the command's
  *Exportable variables* (`declared_env_vars` in the `kai.yml`) are accepted; anything else is
  rejected and flagged in the inspector. Scope and persistence come from the declaration.
* **`notify`** — a system notification (only shown as a toast when the Kai window is not in
  front; always recorded in the notification history).
* **Environment** your program receives: `KIP_VERSION=1`, `KIP_LOCALE=en|pt`, plus the usual
  Kai environment (project variables, parameters).
* **Security:** a KIP program cannot make Kai run anything. `open_url` is http/https only,
  `set_env` is allowlisted, Markdown never loads remote images, secrets are masked in the inspector.

---

## 12. Registering the program in Kai (`kai.yml`)

KIP is a property of a **command** (`type: "command"`, native shell, or Python/Node/PHP code). The file is a
`kai.yml`, and Kai reads a deliberate YAML **subset** — anything outside it is misread *without an
error*. The rules in short:

* two spaces per level, **never tabs**; comments only on their own line;
* **double-quote every string value** (`name: "Deploy"`); text-typed values must be quoted even when they
  look like numbers or booleans (`env_vars` values, parameter `default`/`options`, header values) — an
  unquoted `PORT: 8080` becomes an empty string;
* numbers and flags are **bare** (`kip_auto_close_delay_sec: 3`, `kip: true`);
* multi-line text (a program written in the file, a long command) is a **block scalar**: `command: |`
  followed by the indented lines — everything in it is literal text;
* no anchors/aliases/tags; omit a key to get its default.

```yaml model kip_command
# The keys that matter for a KIP command (the full command model is in the Kai manifesto §3.1).
- name: "Deploy wizard"
  # The program, or the command line that starts it. Kai NEVER edits it: if the tool needs a flag
  # to speak KIP (`mytool --kip`), write it here yourself. A native KIP program needs nothing.
  command: "./deploy.sh"
  # Required: runs the command as a KIP session.
  kip: true
  # Open the KIP view in its own window instead of the output panel. Default: false.
  kip_window: true
  # With kip_window: close the window by itself after a SUCCESSFUL session (a failure or a cancel
  # keeps it open). Default: false.
  kip_auto_close: true
  # Seconds before closing, 0-60 (a bare number). Default: 2.
  kip_auto_close_delay_sec: 3
  # The ONLY names a `set_env` message may export (§11). Others are rejected.
  declared_env_vars:
    - name: "API_TOKEN"
      persist: true
      scope: "global"
```

* The command runs on pipes, **never in a PTY**; `{{VAR}}` interpolation and the parameters form work in
  the command line as usual (not inside Python/Node/PHP code: read `os.environ` / `process.env` / `getenv()`).
* A KIP command **cannot also be**: `is_background`, `interactive_terminal`, `formatted_output`,
  `compact_output`, `open_last_link`, have `responders`, `capture_env`,
  `cron_expression` or `auto_run`; and it cannot be used as a **hook**. (`declared_env_vars` stays
  allowed — that is how `set_env` is authorised.) `kai validate` warns about each of these.
* Only **command**-type commands: HTTP commands have no KIP option.
* `kai <path>` in local CLI mode refuses KIP commands (no UI to draw them); use `kai -g <path>`.

```yaml file
project_name: "Wizards"
icon: "wand-sparkles"
commands:
  - name: "Deploy wizard"
    type: "command"
    description: "Select + flags, danger confirm for production, progress, result with actions."
    command: "./deploy.sh"
    icon: "rocket"
    kip: true
    declared_env_vars:
      - name: "API_TOKEN"
  - name: "Tool with a KIP mode"
    type: "command"
    command: "mytool --kip"
    kip: true
    kip_window: true
    kip_auto_close: true
```

---

## 13. Templates

### 13.1 bash — self-contained, no `jq`, no helper

```bash
#!/usr/bin/env bash
set -u

# Prints one line of JSON. printf-style: send '{"a":"%s"}' "$(esc "$value")"
send() { printf "$1" "${@:2}" | tr -d '\n'; echo; }
# Escapes a value for a JSON string.
esc()  { local s=${1//\\/\\\\}; s=${s//\"/\\\"}; s=${s//$'\n'/\\n}; printf '%s' "${s//$'\t'/\\t}"; }
# First value of "key" in $MSG: string (unescaped), number, true/false or null.
jget() {
    local v
    v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":"(([^"\\]|\\.)*)".*/\1/p')
    [ -z "$v" ] && v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":(true|false|null|-?[0-9.]+).*/\1/p')
    v=${v//\\\\/$'\001'}; v=${v//\\\"/\"}; v=${v//\\\//\/}; v=${v//\\n/$'\n'}; v=${v//$'\001'/\\}
    printf '%s' "$v"
}
# Waits for Kai's next message into $MSG. A "cancel" means the user gave up.
recv() { IFS= read -r MSG || exit 130; [ "$(jget type)" = cancel ] && exit 130; return 0; }

send '{"kip":1,"type":"hello","title":"Greeter"}'
send '{"kip":1,"type":"prompt","id":"name","title":"Who are you?","fields":[{"name":"who","type":"text","label":"Name","required":true}]}'
recv
who=$(jget who)
send '{"kip":1,"type":"done","title":"Hello, %s!"}' "$(esc "$who")"
```

Pitfalls specific to bash: always pass user values through `esc`; a `%` inside the printf *format*
must be `%%` (values passed as arguments are safe); `jget` finds the **last** `"key":` in the line,
so avoid reusing a key name at different depths; it does not decode `\uXXXX` (Kai writes UTF-8
directly, so this only matters for ASCII-escaped input).

### 13.2 Python

```python
#!/usr/bin/env python3
import json, sys

def send(**message):
    print(json.dumps({"kip": 1, **message}, ensure_ascii=False), flush=True)

def recv():
    line = sys.stdin.readline()
    if not line:                      # Kai closed stdin
        sys.exit(130)
    msg = json.loads(line)
    if msg.get("type") == "cancel":
        sys.exit(130)
    return msg

send(type="hello", title="Greeter")
send(type="prompt", id="name", title="Who are you?",
     fields=[{"name": "who", "type": "text", "label": "Name", "required": True}])
answer = recv()
send(type="done", title=f"Hello, {answer['values']['who']}!")
```

### 13.2b Python, Node or PHP written directly in Kai (no file, no glue)

If the command's `language` is `"python"`, `"node"` or `"php"` (and `kip` is on), the command text IS the program and
Kai injects a `kip` module: `hello` is sent for you, output is unbuffered, Cancel exits with 130. There is no
`send`/`recv` to write. `{{VAR}}` is not replaced in this code; variables arrive in `os.environ` / `process.env` /
`getenv()`.

```yaml
- name: "Greeter"
  language: "python"
  kip: true
  command: |
    import kip
    v = kip.prompt([{"name": "who", "type": "text", "label": "Name", "required": True}],
                   id="name", title="Who are you?")
    kip.done(title="Hello, " + v["who"] + "!")
```

Python — `kip.prompt(fields, id=, title=, on_change=, on_chip=, validate=, ...)` returns the values dict; `kip.patch(id, fields=, remove=, chips=)` sends a spontaneous patch;
`kip.confirm(text, id=, danger=)` returns a bool; `kip.message/markdown/progress/steps/step/table/notify/set_env/done`
send the message of the same name; `kip.send(type, **fields)` sends anything else; `kip.Back` is raised on Back;
`validate(values)` returns `{field: message}` to answer `invalid`; `on_change(field, values)` returns the fields to patch.

Node — the same API with a global `kip` (or `require('kip')`), promises and top-level `await`:
`const v = await kip.prompt([...], {id: 'name', title: '...'})`, `kip.done({title: '...'})`,
`kip.setEnv(name, value)`, options `onChange`, `onChip`, `validate`.

PHP — the class `Kip` (always available, no `require`; PHP 7.4+), static methods and an options array instead of
keyword arguments: `$v = Kip::prompt([...], ['id' => 'name', 'title' => '...'])`, `Kip::confirm($text, ['id' => ...])`,
`Kip::progress(50, 'Working')`, `Kip::done('Title')` or `Kip::done(['title' => ..., 'actions' => [...]])`,
`Kip::setEnv($name, $value)`, `Kip::patch($id, $fields)`, options `onChange`, `onChip`, `validate` (closures);
`KipBack` is thrown on Back. PHP sends `[]` for an empty object: write `(object)[]` where the protocol wants one.

Prefer this over hand-written JSON whenever the program lives inside Kai; use 13.1/13.2 for a standalone script.

A Python or Node command also gets the **`kai` module** (no KIP needed for it; PHP has none): `kai.notify(...)`, `kai.commands()`, `kai.env.use(...)`, `kai.run(name)`, `kai.ps()`, `kai.kill(...)`, `kai.show()` talk to the running Kai app — handy for a wizard that, say, sends a tray notification when it finishes or starts a follow-up command. Failures raise `kai.Error`. Full table in the Kai manifesto §5.

A script that runs as a **file** (your own `tool.py`, `tool.js` or `tool.php`) finds the same modules when it is
called from a `native` command with `kip: true`: Kai exports `PYTHONPATH`, `NODE_PATH` (CommonJS `require` only) and
`PHP_INI_SCAN_DIR` (the class `Kip` is already there; `php -r` does not read it — use
`require getenv('KAI_MODULES') . '/kip.php';`) pointing at a folder of its own, in front of the user's values. No
`sys.path` hack, no copy of `kip.py` next to the script. It does not cross WSL/Docker/SSH profiles, and a native
command without `kip: true` gets nothing.

### 13.2c The shell, written directly in Kai (no file, no glue)

A **native** command with `kip: true` that runs in a POSIX shell (Linux, macOS, WSL and other POSIX targets) gets a
`kip` function before its text runs: **the same verbs and options as `kai kip` (§13.4), without the `kai` binary and
without `jq`**, so it also works inside WSL, containers and SSH. `hello` is sent for you. Write `kip ...`, not
`kai kip ...`. It is a normal native command: `{{VAR}}` is replaced as always, and the shell is bash (sh where there
is no bash).

```yaml file example shell-greeter
project_name: "Shell greeter"
icon: "terminal"
commands:
  - name: "Where to?"
    type: "command"
    kip: true
    icon: "terminal"
    command: |
      kip prompt --id where --title "Where to?" --field select env Environment --options dev,staging,prod --required
      kip recv
      target=$(kip get "$KIP_MSG" values.env)
      kip progress 50 "Deploying..."
      kip done --title "Deployed to $target"
```

A bigger one, with a `watch` field that rewrites another field (`change` → `patch` with the same `seq`),
server-side validation (`invalid`, then `kip recv` again) and a *danger* confirm — the same wizard as the Python
one of §13.5-B:

```yaml file example shell-wizard
project_name: "Shell wizards"
icon: "terminal"
commands:
  - name: "Deploy"
    type: "command"
    kip: true
    command: |
      kip prompt --id target --title "Deploy" \
        --field text app "App name" --required \
        --field select env Environment --watch --default dev --options dev,prod \
        --field select region Region --options local
      while :; do
        kip recv
        case $(kip get "$KIP_MSG" type) in
          change)
            if [ "$(kip get "$KIP_MSG" values.env)" = prod ]; then regions=eu-west-1,us-east-1; else regions=local; fi
            kip patch --id target --seq "$(kip get "$KIP_MSG" seq)" --field select region Region --options "$regions" ;;
          response)
            app=$(kip get "$KIP_MSG" values.app)
            case $app in
              *[!a-z0-9-]*) kip invalid --id target --error app="Use lowercase letters only"; continue ;;
            esac
            break ;;
        esac
      done
      env=$(kip get "$KIP_MSG" values.env)
      region=$(kip get "$KIP_MSG" values.region)
      if [ "$env" = prod ]; then danger=--danger; else danger=; fi
      kip confirm --id sure --text "Deploy $app to $env?" $danger
      kip recv
      if [ "$(kip get "$KIP_MSG" values.confirmed)" = true ]; then
        kip progress 100 "Done"
        kip done --title "Deployed $app" --text "Region: $region"
      else
        kip done --title "Cancelled" --level warning
      fi
```

* Verbs: `hello prompt confirm patch invalid message markdown progress steps step table notify set-env done
  chip-result`, plus `raw` and `get`. `kip --help` lists them.
* `kip recv` reads Kai's next message into `$KIP_MSG` and exits the script with code **130** on Cancel (or a closed
  stdin). `kip get '<json>' <path>` extracts a value (`values.env`, `items.0.id`) and exits 2 if the path is missing.
  After an `invalid` answer, `kip recv` again — do not re-send the prompt.
* A prompt with `watch` fields or chips: loop on `kip recv` and look at `$(kip get "$KIP_MSG" type)`; answer a
  `change` with `kip patch --id ID --seq "$(kip get "$KIP_MSG" seq)" ...` (a `patch` without `--seq` is spontaneous
  and always applies).

**Limits to respect when you generate this code:**

* `awk` (mawk, gawk and busybox all work) must exist where the command runs; the inline loader also needs `base64`
  and `gzip`. No `jq` is needed by the helper (your own program may use it).
* The helper is POSIX `sh`: it keeps its state in `_kip_*` variables (never reuse those names) and `kip recv` must run
  in the main script — **not inside `$( )` or a pipe**, or the answer is lost with the subshell.
* `kip raw` only checks the basic shape (a JSON object with `kip` and a known `type`); `kip get` prints exponent
  numbers as written and drops NUL bytes. Helper messages are in English.
* On Windows without a POSIX target (plain cmd.exe / PowerShell) there is no `kip` function: use a WSL target, or
  write the program in Python, Node or PHP. A text too long for the command line (cmd.exe cuts at 8191
  characters) is written by Kai to a file of its own and loaded from there — nothing for you to do.

### 13.3 Skeleton for a real wizard

```
hello
loop:                                  # one iteration per Back
    prompt(step 1)  →  recv  →  validate? invalid → recv (do NOT re-send the prompt)
    confirm (danger?, back:true)  →  recv  →  back? continue : declined? done(warning)
progress / steps  →  do the work, updating step states
done (actions)  →  exit 0              # exit non-zero on failure
```

### 13.4 Helper (optional)

If the `kai` binary is on the program's PATH, `kai kip <verb> …` prints a message
(`kai kip prompt --id p --field text name "Name" --required`, `kai kip get "$resp" values.name`…).
It is a convenience only: **prefer self-contained programs** (or, when the script lives inside Kai, a native command with `kip: true` and the injected `kip`, §13.2c) — when the command runs inside WSL or a
container, `kai` may simply not exist there. `kai kip --help` lists every verb; `kai kip raw '<json>'`
validates a message with Kai's own parser and prints it normalised.

### 13.5 Complete worked examples (runnable as written)

**A. A deploy wizard in bash.** Register it with the first `kai.yml` of §12, save this file next to
it as `deploy.sh` and `chmod +x deploy.sh`. It shows: select + flags, a *danger* confirm with Back
(only for production), progress, and a result with actions. Plain bash: no `jq`, no helper.

```bash example deploy
#!/usr/bin/env bash
set -u

# Prints one line of JSON, printf-style: send '{"a":"%s"}' "$(esc "$value")".
send() { printf "$1" "${@:2}" | tr -d '\n'; echo; }
# Escapes a value for use inside a JSON string.
esc() { local s=${1//\\/\\\\}; s=${s//\"/\\\"}; s=${s//$'\n'/\\n}; printf '%s' "${s//$'\t'/\\t}"; }
# First value of "key" in $MSG: a string (unescaped), number, true/false or null.
jget() {
    local v
    v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":"(([^"\\]|\\.)*)".*/\1/p')
    [ -z "$v" ] && v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":(true|false|null|-?[0-9.]+).*/\1/p')
    v=${v//\\\\/$'\001'}; v=${v//\\\"/\"}; v=${v//\\\//\/}; v=${v//\\n/$'\n'}; v=${v//$'\001'/\\}
    printf '%s' "$v"
}
# Waits for Kai's next message into $MSG. A "cancel" means the user gave up.
recv() { IFS= read -r MSG || exit 130; [ "$(jget type)" = cancel ] && exit 130; return 0; }

send '{"kip":1,"type":"hello","title":"Deploy","version":"1.4.2"}'

while true; do
    send '{"kip":1,"type":"prompt","id":"target","title":"Where to deploy?","submit_label":"Next",
        "fields":[
            {"name":"env","type":"select","label":"Environment","required":true,"default":"staging",
             "options":[{"value":"dev","label":"Development"},
                        {"value":"staging","label":"Staging","description":"Mirror of production"},
                        {"value":"prod","label":"Production","description":"Customer facing"}]},
            {"name":"opts","type":"flags","label":"Options",
             "options":[{"name":"dry","label":"Dry run","default":false},
                        {"name":"notify","label":"Notify the team","default":true}]}
        ]}'
    recv
    env=$(jget env)
    dry=$(jget dry)

    if [ "$env" = prod ]; then
        send '{"kip":1,"type":"confirm","id":"sure","title":"Deploy to production?","danger":true,
            "text":"Version 1.4.2 goes live for every customer.",
            "confirm_label":"Deploy now","cancel_label":"Not now","back":true}'
        recv
        [ "$(jget type)" = back ] && continue
        if [ "$(jget confirmed)" != true ]; then
            send '{"kip":1,"type":"done","level":"warning","title":"Deploy cancelled","text":"Nothing was deployed."}'
            exit 0
        fi
    fi
    break
done

suffix=''; [ "$dry" = true ] && suffix=' (dry run)'
send '{"kip":1,"type":"message","level":"info","text":"Deploying version 1.4.2 to %s%s…"}' "$(esc "$env")" "$suffix"
for pct in 20 60 100; do
    send '{"kip":1,"type":"progress","value":%s,"label":"Uploading artifacts… %s%%"}' "$pct" "$pct"
done
send '{"kip":1,"type":"done","title":"Deployed to %s","text":"Version 1.4.2 is live%s.",
    "actions":[{"type":"open_url","label":"Open the site","url":"https://example.com"},
               {"type":"copy","label":"Copy release id","value":"rel-%s"}]}' \
    "$(esc "$env")" "$suffix" "$(date +%s)"
```

**B. A Python wizard written directly in the `kai.yml`** (no file, no glue; `kip` is injected). It
shows: a `watch` field that rewrites another field's options (`on_change`), server-side
validation (`validate` → `invalid`, the form stays open with the message), and a *danger* confirm.

```yaml file example python-wizard
project_name: "Python wizards"
icon: "wand-sparkles"
commands:
  - name: "Deploy (Python)"
    type: "command"
    language: "python"
    kip: true
    command: |
      import kip

      REGIONS = {"dev": ["local"], "prod": ["eu-west-1", "us-east-1"]}

      def on_change(field, values):
          # The watched `env` changed: replace the `region` field (same name = replaced).
          regions = REGIONS[values["env"]]
          return [{"name": "region", "type": "select", "label": "Region",
                   "options": regions, "default": regions[0]}]

      def validate(values):
          if not values["app"].islower():
              return {"app": "Use lowercase letters only"}

      answer = kip.prompt(
          [
              {"name": "app", "type": "text", "label": "App name", "required": True},
              {"name": "env", "type": "select", "label": "Environment", "watch": True,
               "default": "dev", "options": ["dev", "prod"]},
              {"name": "region", "type": "select", "label": "Region", "options": ["local"]},
          ],
          id="target", title="Deploy", on_change=on_change, validate=validate,
      )
      sure = kip.confirm("Deploy " + answer["app"] + " to " + answer["env"] + "?",
                         id="sure", danger=(answer["env"] == "prod"))
      if sure:
          kip.progress(100, "Done")
          kip.done(title="Deployed " + answer["app"], text="Region: " + answer["region"])
      else:
          kip.done(title="Cancelled", level="warning")
```

**C. A table with chips** (page through data, act on the selection) — every line is one message:

```jsonl
{"kip":1,"type":"hello","title":"Customers"}
{"kip":1,"type":"prompt","id":"pick","title":"Customers","fields":[{"name":"customer","type":"table","label":"Customer","row_key":"id","required":true,"searchable":true,"page_size":5,"columns":[{"key":"id","label":"ID"},{"key":"name","label":"Name"}],"rows":[{"id":"c-1","name":"Acme"},{"id":"c-2","name":"Globex"}]}],"chips":[{"id":"orders","label":"Orders","icon":"info","description":"Last orders of this customer","requires":["customer"]},{"id":"delete","label":"Delete","icon":"trash-2","danger":true,"requires":["customer"],"confirm":{"title":"Delete this customer?","text":"This cannot be undone.","confirm_label":"Delete","cancel_label":"Keep"}}]}
{"kip":1,"type":"chip_result","chip":"orders","state":"running","text":"Loading…"}
{"kip":1,"type":"chip_result","chip":"orders","state":"success","title":"Orders · Acme","text":"- #1042 — 3 items\n- #1039 — 1 item"}
{"kip":1,"type":"done","title":"Closed","level":"success"}
```

---

## 14. Design guidance

* **One decision group per prompt.** Prefer 3–6 related fields over one giant form; use `group` for
  advanced options (collapsed by default).
* **Good defaults, stable ids.** Remembered answers make the second run a two-click affair.
* **Validate on the server side with `invalid`**, with a message per field and a short summary.
* **Destructive = `danger` confirm** (or a chip with `confirm`+`danger`) stating exactly what will
  happen, with a decline label that says what is kept ("Keep current data").
* **Show progress** for anything over ~1 s: `progress` for one task, `steps` for a list.
* **Finish with `done`**: say what happened and offer the next action (`open_url`, `reveal`, `copy`).
* **Fail loudly.** On error print a `done` with `"level":"error"` and exit non-zero; put details on
  stderr.
* Keep a **plain mode** if the tool is also used in a terminal: speak KIP only when asked
  (`--kip`, or when `KIP_VERSION` is set in the environment).
* Do not print secrets to stderr or in `message`s. Use `secret` fields; their values are masked in
  the inspector but **are** sent to you in the `response`.

---

## 15. Testing without Kai

1. **Eyeball the output:** run the program and pipe canned answers:
   `printf '%s\n' '{"kip":1,"type":"response","id":"name","values":{"who":"Ana"}}' | ./greeter.sh`
2. **Validate every line you print** with Kai's own parser: `./prog < answers | while read -r l; do kai kip raw "$l" >/dev/null || echo "INVALID: $l"; done`
   (`kai kip raw` exits 2 and explains what is wrong).
3. **Test the unhappy paths**: send `{"kip":1,"type":"cancel"}`, close stdin early, answer an `invalid`
   twice, press Back.
4. In Kai, open **Details → Protocol** to see every line in both directions, and **Details → Log**
   for stderr and non-protocol stdout.

---

## 16. Generator checklist

Before you hand back a KIP program, verify each item:

- [ ] Every line on stdout is one complete JSON object with `"kip":1` and a known `type`; nothing else
      is printed to stdout (logs go to stderr).
- [ ] Output is flushed after every line.
- [ ] The first message is `hello`.
- [ ] Prompts have unique `id`s and fields have unique `name`s; ids/names are stable between runs.
- [ ] Every user-supplied value that is printed goes through JSON escaping.
- [ ] After `invalid` the program **reads the next answer** instead of re-sending the prompt.
- [ ] Every `change` from a `watch` field gets a `patch` with the same `seq`.
- [ ] `cancel` (and a closed stdin) are handled: clean up and exit.
- [ ] Destructive actions are behind a `confirm` with `"danger":true` (or a chip's `confirm`+`danger`).
- [ ] Picked paths and `reveal` paths use `"path_format":"posix"` if the program may run in WSL.
- [ ] `set_env` names are declared in the command's `declared_env_vars`.
- [ ] In a native shell command with `kip: true` use `kip ...` (never `kai kip`), `kip recv` outside subshells,
      and say in a comment when `awk` is required.
- [ ] The run ends with `done` and the exit code reflects success (`0`) or failure (non-zero).
- [ ] The command is registered in the `kai.yml` with `kip: true` (type `command`), with the KIP flag in
      `command` when the tool needs one, following the YAML rules of the Kai manifesto §1 (double-quoted strings,
      bare numbers/flags, multi-line code as a `|` block).

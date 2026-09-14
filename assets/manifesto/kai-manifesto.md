# The Kai manifesto — writing a `kai.yml`

> Self-contained guide for an **AI model** (or a person) that has to produce a project file for
> Kai: a `kai.yml`. Everything below is what Kai's importer accepts today. It is about *writing
> the file*, not about developing Kai.
>
> **Your job:** output ONE YAML file. Read §1 (Kai reads a deliberate YAML subset) and §2 (what
> the file is), copy from the models (§3–§9) and the complete examples (§10), tick the checklist
> (§11). When you can, run `kai validate kai.yml` and fix every line it reports.
>
> Fence legend used in this guide:
> - ` ```yaml file ` — a **complete, importable file**;
> - ` ```yaml model <name> ` — the **exhaustive list of keys** of one structure, with its
>   defaults in comments. It is a reference to copy from, **never a file to paste whole** (it lists
>   mutually exclusive options on purpose);
> - ` ```yaml ` — a fragment.
>
> Icons: every `icon:` value must be a name from the icon list that follows this guide.

---

## 1. The YAML Kai reads — rules that matter

Kai parses a YAML **subset**. Valid-looking YAML outside it is read wrongly **without any error**
(the file still imports, with wrong or empty values). Write it exactly like this:

1. **Two spaces per level. Never tabs.**
2. **Double-quote every string value**: `name: "Build"`. Keys stay unquoted. (Unquoted text
   works until it looks like a number, a boolean, or contains ` #`.)
3. **Text-typed values must be quoted even when they look like numbers or booleans.** Kai reads
   these places as *text*; an unquoted number or `true` there becomes an **empty string**:
   `env_vars` values (`PORT: "8080"`), `params[].default` (`default: "3"`), `params[].options[]`
   (`["1", "2"]`), `http_config.headers` values, `collections[].entries[].values` (`key: "10"`).
   (A `bool` parameter also accepts `default: true`.)
4. **Numbers and flags are bare**: `order: 2`, `auto_run_delay_sec: 5`, `kip_auto_close_delay_sec: 2`,
   `max_triggers: 3`, `is_background: true`. Quoting them (`"2"`, `"true"`) makes Kai ignore them.
5. **Multi-line text → a block scalar**, never a string continued over several lines:
   ```yaml
   command: |
     npm ci
     npm test
   ```
   `|` keeps the line breaks, `|-` also drops the last one, `>` folds the lines into one (to wrap a
   long one-liner). Inside a block scalar everything is **literal text**: quotes, `#`, `{{VAR}}`,
   `{% if %}` need no escaping and `#` is not a comment.
6. A one-line string with special characters: double quotes with escapes `\"`, `\\`, `\n`;
   Windows paths need doubled backslashes (`"C:\\work"`). Single quotes are accepted too.
7. **Comments only on their own line.** A ` #` after an unquoted value cuts the value.
8. **Not supported** (silently misread): anchors/aliases (`&x`, `*x`), tags (`!!str`), merge keys
   (`<<`), several documents. Flow style only for short one-line lists/maps (`[a, "b"]`,
   `{k: v}`) — prefer block style.
9. **Empty values:** omit the key. A bare `key:` is an *empty mapping*, not an empty string;
   an empty string is `""`, an empty list is `[]`.
10. **Never repeat a key** at the same level (the last one wins).

---

## 2. The file

A `kai.yml` at the **root of a project** — YAML is the only file format Kai uses (a `kai.json`
or `kai.yaml` is not read). It is used by **File → Import Project**
(pick the folder), by `kai import <folder|file>`, and by the CLI (`kai <path…>` inside a folder
that has the file).

Importing creates a **project folder** (named `project_name`) with the commands inside it
(subfolders come from each command's `folder`) and the declared collections.

**Never write ids** (`id`, `folder_id`, `parent_id`): Kai generates them. **Never write a
`kai_export` header**: that is the *Export/Import Configuration* format Kai itself produces — a
different file. Everything below is the project format.

```yaml model project
# Top-level keys of a kai.yml. All optional.
# Name of the project folder. Default: the name of the directory.
project_name: "Shop API"
# Icon of the project folder (default: generic).
icon: "package"
# Terminal segment that makes this folder reachable from the CLI: `kai shop <command>`.
cli_path: "shop"
# One-line description shown by `kai shop --help`.
cli_description: "Shop API tools"
# Variables of the project scope ({{PORT}}); inherited by every command. VALUES ARE TEXT: quote them.
env_vars:
  PORT: "8080"
# The commands (§3).
commands:
  - name: "Dev server"
    command: "npm run dev"
# Tabular data sources (§8).
collections: []
# Notes that travel with the project: quick text kept in Kai (§8.1).
notes:
  - name: "Release checklist"
    content: "1. Bump the version\n2. Tag"
# Only metadata of SUBFOLDERS (§2.1): icon / cli_path / cli_description by path.
folders:
  - path: "Backend"
    icon: "server"
```

Smallest useful file:

```yaml file
project_name: "My Project"
icon: "terminal"
env_vars:
  PORT: "8080"
commands:
  - name: "Dev server"
    type: "command"
    command: "npm run dev"
    is_background: true
  - name: "Migrate"
    type: "command"
    command: "npm run db:migrate"
```

### 2.1 Subfolders

A command or collection goes into a subfolder with `folder:` (relative to the project root,
nestable with `/`, created on demand). The top-level `folders:` list never creates anything — it
only gives an **already used** path an icon and/or CLI metadata:

```yaml model folder_meta
# One entry of the top-level `folders:` list. `path` is required and must equal a `folder:` used
# by some command/collection (relative to the project root, WITHOUT the project name).
- path: "Backend/API"
  # Icon of that subfolder (default: generic).
  icon: "webhook"
  # CLI segment of that subfolder (default: transparent in the CLI).
  cli_path: "api"
  # Description shown by `--help` (default: none).
  cli_description: "API tools"
```

Nothing else can be set on a subfolder from this file (no `env_vars`, `hidden`, `order`, …). Do
not write `is_project` or `name` there.

---

## 3. Commands

### 3.1 The complete command model

Every key a command accepts. `name`, `type` and `command` (or `http_config` for `type: "http"`) are
the only ones you normally need. The value shown is a *non-default sample*; the default is in
the comment above it. Many combinations are exclusive (a KIP command cannot also be
`is_background`, an `http` command has no `command`, …) — see §3.3.

```yaml model command
- name: "Deploy staging"
  # Hint shown at the top of the parameter form. Default: none.
  description: "Builds the app and ships it to staging."
  # REQUIRED in the file: "command" or "http". ("shell" is accepted as an old alias of "command".)
  type: "command"
  # Icon name from the icon list. Default: generic icon.
  icon: "rocket"
  # Subfolder of the project, nestable with "/". Default: the project root. (Project file only.)
  folder: "Deploy/Staging"
  # What to run. Native: a shell line/script with {{VAR}} and {% if %} (§4). python/node/php: code (§5).
  command: "./scripts/deploy.sh {{TARGET}}"
  # How `command` is run: "native" (default) | "python" | "node" | "php".
  language: "native"
  # Only with a non-native language: a shell line that starts the interpreter. Default: the global one.
  interpreter: "uv run python"
  # Working directory. Absent = inherit the project directory; a path = its own (a relative path
  # resolves over the inherited one); null = none (the target's default directory).
  working_dir: "{{PROJECT_PATH}}/deploy"
  # Long-running process that stays tracked. Default: false.
  is_background: true
  # Collapse repeated blank lines / trailing spaces in the output. Default: false.
  compact_output: true
  # Hide the Kai window while this command runs. Default: false.
  hide_on_run: true
  # Treat any exit code as success. Default: false.
  ignore_exit_code: true
  # Keep it out of the tree until "Show hidden" is on. Default: false.
  hidden: true
  # Capture the environment the command exports, but only the names in declared_env_vars. Default: false.
  capture_env: true
  # Names (and options) of the variables capture_env / KIP set_env may export (§9.4).
  declared_env_vars:
    - name: "GH_TOKEN"
      persist: true
      scope: "global"
  # On success open the last http(s):// URL found in the output. Default: false.
  open_last_link: true
  # Render the output as a real terminal (vim, htop, REPLs). Default: false.
  interactive_terminal: true
  # Render each JSON log line as a card; non-JSON lines stay text. Default: false.
  formatted_output: true
  # Run as a KIP session: the program draws native screens (see the KIP manifesto). Default: false.
  kip: true
  # With kip: open the KIP view in its own window. Default: false.
  kip_window: true
  # With kip + its own window: close it by itself after a SUCCESSFUL session. Default: false.
  kip_auto_close: true
  # Seconds to wait before closing, 0-60 (a bare number). Default: 2.
  kip_auto_close_delay_sec: 5
  # Name of a terminal profile of THIS machine (Settings → Execution Profiles), e.g. "WSL". Default: none.
  terminal_target: "WSL"
  # Run automatically when Kai starts. Default: false.
  auto_run: true
  # Seconds to wait after startup before an auto_run fires (a bare number). Default: 0.
  auto_run_delay_sec: 5
  # Schedule: 5-field cron (minute hour day-of-month month day-of-week), evaluated in UTC. Default: none.
  cron_expression: "0 9 * * 1-5"
  # Notify with the result at every scheduled run. Default: false.
  cron_notify_on_run: true
  # Position among the siblings of the same folder, lower first (a bare number). Default: alphabetical.
  order: 1
  # Request configuration of an http command (§6).
  http_config:
    method: "GET"
    url: "{{BASE_URL}}/health"
  # The form filled in before running (§7). The key is `params`, NOT `parameters`.
  params:
    - name: "TARGET"
      type: "text"
  # Other commands, BY NAME, to run before / after / on cleanup (§9.1).
  hooks:
    pre: ["Login"]
    post: ["Notify"]
    cleanup: ["Teardown"]
  # Guards that decide whether the command runs at all (§9.2).
  execution_conditions:
    - left: "{{TOKEN}}"
      op: "not_exists"
  # How the guards combine: "and" (default) | "or".
  condition_combinator: "or"
  # When a guard fails: "success" (default, skip quietly) | "failure" (skip and fail the pipeline).
  condition_skip_behavior: "failure"
  # Auto-answers to prompts printed by the command (§9.3).
  responders:
    - name: "Confirm"
      pattern: "\\[y/N\\]"
      response: "y"
  # Makes the command reachable from the terminal: `kai <folder cli_path> <cli_path>`. Default: GUI only.
  cli_path: "deploy"
  # CLI runs: "default" = the command's own working_dir; "invocation" = where you typed `kai`.
  cli_working_dir: "invocation"
```

Keys Kai writes for its own bookkeeping — **never write them**: `id`, `folder_id`,
`last_param_values`, `kip_last_values`, `param_usage_history` (and a collection's `source_path`). Kai itself
no longer writes them to exported or synced files; they are only read if an old file has them.

### 3.2 `working_dir`

Three states, and only three: **absent** (inherit the project directory — what you almost always
want, so leave it out); a **string** (own directory; relative paths are resolved over the
inherited one; `{{VAR}}` allowed); **`null`** (no directory at all). `"{{PROJECT_PATH}}"` alone
means the same as absent.

### 3.3 Combinations that do not work

- `type: "http"` → needs `http_config`; ignore `command`, `language`, `interpreter`, `kip`,
  `interactive_terminal`, `responders`, `is_background`.
- `kip: true` → only for `type: "command"`, and it **ignores** (and `kai validate` warns about)
  `interactive_terminal`, `formatted_output`, `compact_output`, `open_last_link`,
  `responders`, `capture_env`, `is_background`, `cron_expression`, `auto_run`; a KIP command also
  cannot be used as a hook. `kip_window`, `kip_auto_close*` need `kip: true`.
- `interactive_terminal` and `formatted_output`: pick one.
- `language: "python"|"node"|"php"` → `capture_env` has no effect; `{{VAR}}` is **not** replaced in the code (§5).
- `interpreter` only with a non-native language.
- `cron_expression` is evaluated in **UTC**; an invalid expression never fires.

---

## 4. Variables and text templates

`{{NAME}}` is replaced right before the command runs. Names use letters, digits, `_` and `.`.
It works in: a **native** `command`, `working_dir`, `http_config` (`url`, header values, `body`),
`responders[].response`, and `execution_conditions[].left/right`. It is **not** replaced in
python/node/php code (§5).

**Precedence** (the rightmost wins): `Global (active Environment) < Project/Folder (env_vars, inherited from
parents) < Dynamic (extracted from HTTP responses / captured env) < Parameters (the form)`.
A missing variable becomes an **empty string** (with a log warning); Kai never fails because of it.

| Variable | Value |
|---|---|
| `{{PROJECT_PATH}}` | Absolute path of the project directory (always available). |
| any `env_vars` key | From the file. |
| `{{$uuid}}` | UUID v4. `{{$randomUuidHex}}`: 32 hex chars. |
| `{{$timestamp}}` / `{{$timestampMs}}` | Epoch seconds / milliseconds. |
| `{{$isoTimestamp}}` | ISO-8601 date-time in UTC. |
| `{{$randomInt}}` / `{{$randomInt.N}}` | Integer in `[0, 100000)` / `[0, N)`. |

An unknown `{{$token}}` becomes an empty string.

**Conditional text.** Before variables are replaced, a block can choose which text survives:

```yaml
command: 'docker compose up {% if {{BUILD}} == "true" %}--build{% else %}--no-build{% endif %} {{SERVICE}}'
```

`{% if A op B %}` … optional `{% else %}` … `{% endif %}`; blocks nest. Operators: `==`, `!=`, `>`,
`<`, `>=`, `<=` (the ordering ones need numbers on both sides, otherwise the condition is false).
Operands are `{{VAR}}`, quoted text or a number. With **no operator** the condition is true when the
value is not empty, not `false` and not `0`: `{% if {{VERBOSE}} %}-v{% endif %}`.

**What a parameter injects** (name = the parameter's `name`):

| Injected | When |
|---|---|
| `{{name}}` | always — the value (a `bool` gives `true`/`false`; a multi-select gives the values joined by `,`) |
| `{{name.field}}` | `select` tied to a collection: any field of the chosen entry (§8) |
| `{{name.field__all}}` | multi-select tied to a collection: the field of **all** chosen entries, comma-joined |
| `{{name__label}}` / `{{name__labels}}` | `select` with fixed `options`: the shown label of the choice(s) (for options written `"Label:value"`) |
| `{{name.end}}` | `date` with `date_range: true`: the end of the range (`{{name}}` is the start) |

---

## 5. Python, Node and PHP commands

With `language: "python"`, `"node"` or `"php"`, `command` is **code**, not a shell line: Kai runs it as
`python3 -u -c <code>` / `node -e <code>` / `php -r <code>` itself (no quoting, no heredoc, stdin stays free; Node code
runs inside an async function, so top-level `await` works). Write the code as a block scalar.

```yaml
- name: "Disk report"
  language: "python"
  command: |
    import os, shutil
    free = shutil.disk_usage('.').free // 2**30
    print(os.environ.get("ENV_NAME", "dev"), free, "GiB free")
```

Rules:
- **Never use `{{VAR}}` inside the code** (it is not replaced; it collides with f-strings and template
  strings). Read variables and parameters from the environment: `os.environ["NAME"]` /
  `process.env.NAME` / `getenv("NAME")`.
- `interpreter` (optional) is a shell line, resolved where the command runs (it may have arguments,
  e.g. `uv run python`). Omit it to use the global one (Settings → Languages).
- The exit code decides success (`sys.exit(2)`, `process.exitCode = 2`). `capture_env` does nothing here.
- `is_background`, `hooks`, `execution_conditions`, `cron_expression`, `auto_run`, `params`,
  `terminal_target` work as for any command.

**The `kai` module** is injected (no install) and talks to the running Kai, like the `kai` CLI:

| Python | Node (global `kai`, every call returns a promise) | Does |
|---|---|---|
| `kai.notify(msg, title=None, level="info")` | `await kai.notify(msg, {title, level})` | tray notification (`info`/`warning`/`error`) |
| `kai.commands()` | `await kai.commands()` | names of the registered commands |
| `kai.env.list()` / `.active()` / `.use(name)` | same names | environments |
| `kai.run(name)` | `await kai.run(name)` | start another command (fire and forget) |
| `kai.ps()` / `kai.kill(target)` | same names | tracked processes / stop one |
| `kai.show()` | `await kai.show()` | bring Kai to the front |
| `kai.import_project(path)` | `await kai.importProject(path)` | import a `kai.yml` / folder |

Failures raise `kai.Error`. With `kip: true` a `kip` module is injected too (see the KIP manifesto).

### 5.1 The shell is `native`

There are no separate bash/sh/PowerShell languages: a **native** command already is a shell script (bash, and sh
where there is no bash), with `{{VAR}}` and `{% if %}` replaced as always. With `kip: true`, in a POSIX shell
(Linux, macOS, WSL and other POSIX targets), a `kip` function is defined before the text runs, with the same verbs
as `kai kip` and **no `kai` binary or `jq`** — see the KIP manifesto §13.2c. The old `language: "bash"|"sh"|"pwsh"`
values still load (as `native`) with a validator warning.

---

## 6. HTTP commands

`type: "http"` with an `http_config`.

```yaml model http_config
# Request of an http command.
http_config:
  # GET (default) | POST | PUT | PATCH | DELETE | QUERY
  method: "POST"
  # Required. {{VAR}} allowed.
  url: "{{BASE_URL}}/auth/login"
  # Header values are TEXT: quote them. {{VAR}} allowed. Default: none.
  headers:
    Content-Type: "application/json"
    Authorization: "Bearer {{AUTH_TOKEN}}"
  # Request body as text ({{VAR}} and {{$token}} allowed). Default: none. Use a block scalar for JSON.
  body: |
    {"user": "{{USER}}", "nonce": "{{$uuid}}"}
  # Pull fields of the JSON response into dynamic variables for the next commands/hooks.
  env_extractors:
    - json_path: "token"
      env_var: "AUTH_TOKEN"
```

```yaml model env_extractor
# One entry of http_config.env_extractors.
# Optional label shown in the variables inspector. Default: none.
- name: "Login token"
  # Dot path through the JSON response OBJECTS (e.g. "data.token"; array indices are not supported).
  # "a.b || c" tries each path in order and uses the first that exists.
  json_path: "data.token || token"
  # Name of the dynamic variable that receives it ({{AUTH_TOKEN}}). Must be a valid variable name.
  env_var: "AUTH_TOKEN"
  # Keep it after Kai restarts. Default: false.
  persist: true
  # "project" (default) = the current project; "global" = visible everywhere.
  scope: "global"
```

The extracted variable has priority over global/project values of the same name.

---

## 7. Parameters (the form shown before running)

```yaml model parameter
# One entry of `params`. `name` and `type` are REQUIRED in the file; the rest depends on the type.
# Identifier. Becomes {{name}} in the command (letters, digits, `_`; start with a letter or `_`).
- name: "TARGET"
  # Label shown in the form. Default: the name.
  label: "Environment"
  # text (default) | number | bool | select | file | textarea | json | date
  type: "select"
  # Initial value, TEXT (may contain {{VAR}}). For a `bool`: "true" / "false" (or bare true/false).
  default: "staging"
  # select only: the fixed choices, TEXT. An item "Label:value" shows Label and injects value.
  options: ["dev", "staging", "prod"]
  # select with options only: checkbox list; the checked values are joined by a comma. Default: false.
  multi_select: true
  # select only: NAME of a collection of this file — the choices are its entries (§8).
  collection: "Users"
  # With `collection`: the schema field shown in the picker (e.g. "value").
  collection_display_field: "value"
  # file only: "file" (default) | "folder" | "both".
  pick_mode: "folder"
  # file only: convert the picked path — "native" (default) | "posix" | "windows" (useful under WSL).
  file_path_format: "posix"
  # Starts hidden behind a "Provide <label>?" checkbox. Default: false.
  optional: true
  # Marks it with * and blocks OK until filled. Default: false. No effect when optional is true.
  required: true
  # Placeholder/help text of text and textarea fields. Default: none.
  description: "Where the build is shipped"
  # date only: "date" (default) | "time" | "datetime".
  date_mode: "datetime"
  # date only: pick a start AND an end; the end is {{name.end}}. Default: false.
  date_range: true
  # date only: iso_date (default) | iso_datetime | br_date | us_date | time_24h | time_24h_short |
  #            unix_seconds | unix_millis | custom
  date_format: "custom"
  # date only, with date_format "custom": Qt tokens yyyy yy MM M dd d HH H hh h mm ss AP.
  date_format_custom: "dd.MM.yy HH:mm"
  # Parameters with the same group share one collapsible section (collapsed by default). Default: none.
  group: "Advanced"
```

| `type` | Widget | Value |
|---|---|---|
| `text` | text field | the text |
| `textarea` | multi-line text | the text |
| `number` | integer spinner | an integer, as text |
| `bool` | checkbox | `"true"` / `"false"` |
| `select` | combo, checkbox list, or collection picker | the choice (see §7.1) |
| `file` | field + browse button | absolute path of the file/folder |
| `json` | small JSON editor | the typed JSON, as plain text |
| `date` | date/time picker | formatted per `date_format` |

### 7.1 `select`, three modes

```yaml
params:
  # (a) one fixed choice
  - name: "ENV_TARGET"
    label: "Environment"
    type: "select"
    default: "dev"
    options: ["dev", "staging", "production"]
  # (b) several fixed choices -> {{SERVICES}} = "api,worker"
  - name: "SERVICES"
    type: "select"
    multi_select: true
    options: ["api", "worker", "web", "cron"]
  # (c) tied to a collection (§8)
  - name: "customer"
    type: "select"
    collection: "Customers"
    collection_display_field: "value"
```

### 7.2 Other types

```yaml
params:
  - name: "REPLICAS"
    type: "number"
    default: "3"
  - name: "DROP_DB"
    label: "Recreate from scratch?"
    type: "bool"
    default: "false"
  - name: "CERT"
    type: "file"
    pick_mode: "both"
  - name: "WINDOW"
    label: "Deploy window"
    type: "date"
    date_mode: "datetime"
    date_range: true
    date_format: "iso_datetime"
```

Used as `--from "{{WINDOW}}" --to "{{WINDOW.end}}"`. For a `bool`: `if [ "{{DROP_DB}}" = "true" ]; then …`.

---

## 8. Collections

A **collection** is a non-executable table of records (a field schema + rows). It is mostly the source
of a `select` parameter.

```yaml model collection
# One entry of the top-level `collections` list.
# Required. A parameter's `collection` refers to this name.
- name: "Customers"
  # Default: generic.
  icon: "database"
  # Subfolder of the project, nestable with "/". Default: the project root. (Project file only.)
  folder: "CRM"
  # Position among siblings (a bare number). Default: alphabetical.
  order: 1
  # Keep it out of the tree until "Show hidden". Default: false.
  hidden: true
  # Columns. Default: two fields, `key` and `value`.
  schema:
    - name: "key"
      label: "ID"
      type: "key"
  # Rows.
  entries:
    - values:
        key: "1"
        value: "Alice"
```

```yaml model collection_field
# One column of `schema`.
# Stable key, used in entries and in {{param.<name>}}.
- name: "email"
  # Header shown in the grid. Default: the name.
  label: "Email"
  # text (default) | key | value | email | number | url | bool. Unknown types become text.
  type: "email"
  # Show it as a grid column. Default: true.
  visible: false
  # Mask the value and ALWAYS strip it from exports (tokens, test credentials). Default: false.
  secret: true
```

```yaml model collection_entry
# One row of `entries`.
# field name -> value. VALUES ARE TEXT: quote them ("1", not 1).
- values:
    key: "1"
    value: "Alice"
    email: "alice@example.com"
  # Mark as favorite. Default: false.
  favorite: true
```

**Tying a `select` to a collection:** the parameter names it (`collection: "Customers"`); choosing an
entry injects **all** its fields as `{{customer.key}}`, `{{customer.value}}`, `{{customer.email}}`
(and `{{customer.<field>__all}}` with multi-select). Make sure every `{{param.field}}` you write exists
in the `schema`.

### 8.1 Notes

A **note** is quick text kept inside Kai (a checklist, a JSON payload to reformat, a scratch YAML). Notes live in a
folder, show in the command tree with a notebook icon and in the folder's file explorer, and are edited in Kai's editor.
By default a note is **local** to one Kai and never reaches this file. The ones written under `notes:` here are the
ones that **sync**: they are imported with the project and written back by *Sync with file*.

```yaml model note
# One entry of the top-level `notes:` list. All optional except `name`.
- name: "Release checklist"
  # Text of the note. Default: empty.
  content: "1. Bump the version\n2. Tag\n3. Publish"
  # How the note is shown and which editor tools apply: markdown, text, json, yaml or xml. Default: markdown.
  type: "markdown"
  # Icon in the tree (default: a notebook).
  icon: "list-checks"
  # Subfolder of the project the note lives in, like a command's `folder` (default: the project folder itself).
  folder: "Backend"
```

Do not write `id`, `folder_id` or `local` — a note in this file is always a syncing one.

---

## 9. Hooks, conditions, responders, exported variables

### 9.1 Hooks

```yaml model hooks
# Other commands of THIS file, referenced BY NAME (never by id). All three lists are optional.
hooks:
  # Run before the main command. If one fails (exit ≠ 0 / HTTP ≥ 400) the pipeline aborts.
  pre: ["Login"]
  # Run after it succeeds. If one fails the pipeline is marked failed.
  post: ["Notify"]
  # ALWAYS run when execution ends (success, failure, stop, reset) to tear things down.
  cleanup: ["Stop containers"]
```

A name that matches no command in the file is ignored (never breaks the import). A KIP command cannot be a hook.

### 9.2 Execution conditions

A guard that decides whether a command (main or hook) runs. Typical: "log in only if the token is empty
or expired".

```yaml model condition
# One entry of `execution_conditions`.
# Optional label shown in skip/failure log lines. Default: auto-generated.
- name: "Token expired"
  # Interpolated text: {{VAR}}, {{$timestamp}} or a literal.
  left: "{{EXPIRES_AT}}"
  # exists | not_exists | eq | ne | gt | ge | lt | le | contains | not_contains  (default: exists)
  op: "lt"
  # Interpolated text compared with `left`. Ignored by exists / not_exists.
  right: "{{$timestamp}}"
  # Switch this one off without deleting it. Default: true.
  enabled: false
```

`exists`/`not_exists`: `left` is not empty / is empty. `eq`/`ne`: numeric when both sides are numbers,
text otherwise. `gt`/`ge`/`lt`/`le`: numeric only (false otherwise). `contains`/`not_contains`: substring.
Combine several with the command's `condition_combinator` and choose the failure behaviour with
`condition_skip_behavior` (§3.1).

### 9.3 Responders

Auto-answers for interactive scripts that ask `[y/N]`-style questions.

```yaml model responder
# One entry of `responders`.
# Required — an entry without a name is discarded.
- name: "Confirm"
  # Regex matched against the output. Groups are available as \1, \2… in `response`.
  pattern: "\\[y/N\\]"
  # Text sent to the process's stdin, as if typed ({{VAR}} allowed).
  response: "y"
  # Switch it off without deleting it. Default: true.
  enabled: false
  # Fire at most max_triggers times per run. Default: false.
  limit_triggers: true
  # Cap used when limit_triggers is on (a bare number). Default: 1.
  max_triggers: 2
```

### 9.4 Exported variables (`capture_env`, KIP `set_env`)

```yaml model declared_env_var
# One entry of `declared_env_vars`: the ONLY names a command may export.
# Exact variable name. A declared name the process never sets shows up as an EMPTY variable.
- name: "API_TOKEN"
  # Survive closing Kai (e.g. a refresh token). Default: false.
  persist: true
  # "project" (default) = the current project; "global" = visible everywhere.
  scope: "global"
```

```yaml
- name: "gh auth login"
  command: "gh auth login"
  capture_env: true
  declared_env_vars:
    - name: "GH_TOKEN"
```

With `capture_env: true` and no `declared_env_vars`, nothing is captured.

---

## 10. Complete examples

Each of these is a complete file Kai imports as written.

### 10.1 A Node project: parameters, hooks, conditions, subfolders

```yaml file
project_name: "Storefront"
icon: "package"
env_vars:
  PORT: "3000"
  NODE_ENV: "development"
commands:
  - name: "Install"
    type: "command"
    command: "npm ci"
    folder: "Setup"
  - name: "Dev server"
    type: "command"
    description: "Starts the app with hot reload"
    command: "npm run dev -- --port {{PORT}}"
    is_background: true
    open_last_link: true
    hooks:
      pre: ["Install"]
    execution_conditions:
      - name: "Not running in CI"
        left: "{{CI}}"
        op: "not_exists"
  - name: "Migrate"
    type: "command"
    folder: "Database"
    command: |
      if [ "{{DROP}}" = "true" ]; then
        npm run db:reset
      else
        npm run db:migrate
      fi
    params:
      - name: "DROP"
        label: "Recreate from scratch?"
        type: "bool"
        default: "false"
  - name: "Scale"
    type: "command"
    folder: "Ops"
    command: "kubectl scale deploy/{{SVC}} --replicas={{N}}"
    params:
      - name: "SVC"
        label: "Service"
        type: "select"
        options: ["api", "worker", "web"]
        default: "api"
        required: true
      - name: "N"
        label: "Replicas"
        type: "number"
        default: "2"
      - name: "NAMESPACE"
        type: "text"
        default: "default"
        group: "Advanced"
folders:
  - path: "Database"
    icon: "database"
  - path: "Ops"
    icon: "server"
```

### 10.2 A HTTP API: login with token extraction, collection-backed select, JSON body

```yaml file
project_name: "CRM API"
icon: "globe"
env_vars:
  BASE_URL: "http://localhost:8080"
commands:
  - name: "Login"
    type: "http"
    folder: "Auth"
    http_config:
      method: "POST"
      url: "{{BASE_URL}}/auth/login"
      headers:
        Content-Type: "application/json"
      body: |
        {"user": "{{USER}}", "pass": "{{PASS}}", "nonce": "{{$uuid}}"}
      env_extractors:
        - json_path: "token"
          env_var: "AUTH_TOKEN"
    params:
      - name: "USER"
        type: "text"
        default: "admin"
      - name: "PASS"
        type: "text"
  - name: "Get customer"
    type: "http"
    folder: "Customers"
    http_config:
      method: "GET"
      url: "{{BASE_URL}}/customers/{{customer.key}}"
      headers:
        Authorization: "Bearer {{AUTH_TOKEN}}"
    hooks:
      pre: ["Login"]
    execution_conditions:
      - name: "No token yet"
        left: "{{AUTH_TOKEN}}"
        op: "not_exists"
    params:
      - name: "customer"
        label: "Customer"
        type: "select"
        collection: "Customers"
        collection_display_field: "value"
collections:
  - name: "Customers"
    icon: "database"
    folder: "Customers"
    schema:
      - name: "key"
        label: "ID"
        type: "key"
      - name: "value"
        label: "Name"
        type: "value"
      - name: "email"
        label: "Email"
        type: "email"
    entries:
      - values:
          key: "10"
          value: "Alice"
          email: "alice@example.com"
        favorite: true
      - values:
          key: "11"
          value: "Bob"
          email: "bob@example.com"
folders:
  - path: "Auth"
    icon: "key-round"
  - path: "Customers"
    icon: "user"
```

### 10.3 Operations: logs, schedule, terminal, auto-answers, cleanup

```yaml file
project_name: "Ops"
icon: "server"
commands:
  - name: "Stack up"
    type: "command"
    command: "docker compose up -d"
    hooks:
      cleanup: ["Stack down"]
  - name: "Stack down"
    type: "command"
    command: "docker compose down"
    hidden: true
  - name: "Tail API logs"
    type: "command"
    command: "docker compose logs -f api"
    formatted_output: true
    is_background: true
  - name: "Top"
    type: "command"
    command: "htop"
    interactive_terminal: true
  - name: "Tunnel"
    type: "command"
    command: "ngrok http 3000"
    open_last_link: true
  - name: "Nightly backup"
    type: "command"
    command: "./scripts/backup.sh"
    cron_expression: "0 3 * * *"
    cron_notify_on_run: true
  - name: "Reset database"
    type: "command"
    command: "./scripts/reset-db.sh"
    responders:
      - name: "Confirm"
        pattern: "\\[y/N\\]"
        response: "y"
        limit_triggers: true
        max_triggers: 1
  - name: "Open folder"
    type: "command"
    command: "explorer.exe ."
    ignore_exit_code: true
    working_dir: null
```

### 10.4 Python and Node code, written in the file

```yaml file
project_name: "Scripts"
icon: "code"
commands:
  - name: "Disk report"
    type: "command"
    language: "python"
    command: |
      import os, shutil
      free = shutil.disk_usage(".").free // 2**30
      print(os.environ.get("ENV_NAME", "dev"), free, "GiB free")
    params:
      - name: "ENV_NAME"
        type: "text"
        default: "dev"
  - name: "Notify me"
    type: "command"
    language: "node"
    command: |
      await kai.notify("Backup finished", { title: "Backup" });
      console.log("sent");
```

### 10.5 A KIP command (see the KIP manifesto for the program)

```yaml file
project_name: "Wizards"
icon: "wand-sparkles"
env_vars:
  REGION: "eu-west-1"
commands:
  - name: "Deploy wizard"
    type: "command"
    command: "./deploy.sh"
    kip: true
    kip_window: true
    kip_auto_close: true
    kip_auto_close_delay_sec: 3
    declared_env_vars:
      - name: "API_TOKEN"
        scope: "global"
  - name: "Greeter"
    type: "command"
    language: "python"
    kip: true
    command: |
      import kip
      who = kip.prompt([{"name": "who", "type": "text", "label": "Name", "required": True}],
                       id="name", title="Who are you?")
      kip.done(title="Hello, " + who["who"] + "!")
```

### 10.5b Folder documentation (the README)

There is nothing to configure: when a folder (or the project root) is selected, Kai renders **in the Output area** the
`README.md` (or `readme.md`) of the folder's working directory — or, when there is none, the first document it finds
there. A folder whose directory has no document at all shows a button to create the `README.md`. Selecting a command
shows its output as usual; selecting the folder again shows the document. The side tree of the document view (collapsed
by default) lists the other documents and notes.

Inside the document:
- relative links to other `.md`/`.markdown`/`.txt` files open them inside Kai (the bar above has back/forward and
  clickable breadcrumbs; a side tree lists the documents of the folder);
- `http(s)` links open the browser;
- fenced ```` ```mermaid ```` blocks with a `graph`/`flowchart` are drawn as diagrams;
- Kai links: `[Start](kai:run/Dev server)` runs the command named "Dev server" (after a confirmation),
  `[Prod](kai:env/Prod)` activates an environment, `[Open](kai:open/Dev server)` selects a command or folder in the tree.
  Names with spaces work as-is inside `<...>` (`[x](<kai:run/Dev server>)`) or percent-encoded (`Dev%20server`).

```yaml file
project_name: "Shop API"
commands:
  - name: "Dev server"
    type: "command"
    command: "npm run dev"
```

### 10.6 Reachable from the terminal (CLI paths)

```yaml file
project_name: "Platform"
icon: "boxes"
cli_path: "plat"
cli_description: "Platform tools"
commands:
  - name: "Start container"
    type: "command"
    folder: "Docker"
    cli_path: "up"
    command: "docker compose up -d {{service}}"
    params:
      - name: "service"
        type: "text"
        required: true
      - name: "build"
        type: "bool"
        optional: true
  - name: "Where am I"
    type: "command"
    cli_path: "where"
    cli_working_dir: "invocation"
    command: "pwd"
folders:
  - path: "Docker"
    icon: "container"
    cli_path: "docker"
    cli_description: "Container tools"
```

Used as `kai plat docker up api --build=true` and `kai plat where`.

---

## 11. Checklist

- [ ] One YAML document, 2-space indent, **no tabs**, no anchors/aliases/tags (§1).
- [ ] Every string is double-quoted; **text-typed values are quoted** (`env_vars`, `default`, `options`,
      header values, entry `values`); numbers/flags are bare (`order`, `auto_run_delay_sec`, `max_triggers`, …).
- [ ] Multi-line commands/code/bodies use `|` (or `>`); comments only on their own line.
- [ ] Top-level keys are only `project_name`, `icon`, `cli_path`, `cli_description`, `env_vars`, `commands`,
      `collections`, `folders`. No `id`, `folder_id`, `parent_id`, `kai_export`.
- [ ] Parameters are under `params` (not `parameters`); a `select` tied to a collection uses `collection`
      (the collection's **name**) and `collection_display_field`.
- [ ] Every command has `name` **and** `type`, and every parameter has `name` **and** `type` (the validator
      requires them); `type` ∈ {`command`, `http`}; `language` ∈ {`native`, `python`, `node`, `php`}; parameter `type` ∈ {`text`,
      `textarea`, `number`, `bool`, `select`, `file`, `json`, `date`}; HTTP `method` ∈ {`GET`, `POST`, `PUT`,
      `PATCH`, `DELETE`, `QUERY`}.
- [ ] An `http` command has `http_config.url`; a `kip` command has none of the incompatible keys (§3.3).
- [ ] In python/node/php code never `{{VAR}}` — use `os.environ` / `process.env` / `getenv()`. (Native shell text does replace it.)
- [ ] `{{VAR}}` names exist: `env_vars`, a parameter, `{{PROJECT_PATH}}`, a `{{$…}}` token, or an extracted
      variable. `{{param.field}}` matches the collection `schema`.
- [ ] `hooks` and a collection `collection:` refer to **names** that exist in this same file.
- [ ] `folder` nests with `/` and never includes the project name; `folders:` entries use the same paths.
- [ ] `working_dir` only when it differs from the project directory.
- [ ] `cron_expression` is a valid 5-field cron (UTC); `terminal_target` only if the user named a profile.
- [ ] Every `icon` is in the icon list below; omit every key that equals its default.
- [ ] `kai validate kai.yml` reports no error.

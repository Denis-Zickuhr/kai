# The `kai.json` manifesto — a guide to generating commands, folders and collections

> Self-contained document meant to **feed an AI model** (or a human) that
> needs to produce a `kai.json` for a project. Copy this file alongside
> other artifacts (`package.json`, `docker-compose.yml`, `Makefile`,
> binaries, scripts) and the model will have everything it needs to
> produce a correct `kai.json`.
>
> This guide is about **authoring a `kai.json`** — the commands, folders,
> parameters and collections a project imports into Kai. It has nothing
> to do with developing Kai itself; every field, type and example below is
> what Kai's importer actually accepts today.

---

## 1. What `kai.json` is

A file placed at the **root of a project**. When imported (menu **File →
Import Project**, or by pointing at the folder), Kai creates:

- A project **folder** (name = `project_name`), with `PROJECT_PATH` injected.
- The declared **commands**, inside that folder (or in subfolders via `folder`).
- The declared **collections** (tabular data sources), attached to the project.

You don't need a `kai.json` at all to import something useful: turning on
**"Detect generic definitions"** in the Import Project dialog makes Kai also
recognize `package.json` (npm/yarn/pnpm), `docker-compose.yml`,
`requirements.txt`/`pyproject.toml`/`manage.py` (Python), `composer.json`
(PHP) and a `Makefile`, suggesting ready-made commands in their own
subfolder per ecosystem — see [Section 10](#10-generic-detection-without-a-kaijson).

Import is **idempotent by id design**: folder, command and collection ids
are derived deterministically from the project name and item order.

### Minimal envelope

```json
{
  "project_name": "My Project",
  "icon": "shopping-cart",
  "env_vars": { "PORT": "8080", "NODE_ENV": "development" },
  "commands": [
    { "name": "Dev Server", "type": "shell", "command": "npm run dev", "is_background": true },
    { "name": "Migrations", "type": "shell", "command": "npm run db:migrate" }
  ]
}
```

### Accepted top-level keys

| Key | Type | Required | Description |
|---|---|---|---|
| `project_name` | string | no (defaults to the folder name) | Name of the created folder/project. |
| `icon` | string | no | Folder icon (name from the icon pool, e.g. `shopping-cart`, `globe`, `database`, `terminal`). |
| `env_vars` | object `{string:string}` | no | Environment variables in the project's scope. |
| `commands` | array of command | no | Executable commands. |
| `collections` | array of collection | no | Tabular data sources. |

---

## 2. Commands

Each item in `commands` is an object. Fields the parser reads:

| Key | Type | Default | Applies to | Description |
|---|---|---|---|---|
| `name` | string | `""` | both | Name shown in the tree. |
| `description` | string | `""` | both | Optional hint shown at the top of the parameter form when the command runs. |
| `type` | `"shell"` \| `"http"` | `"shell"` | — | Command type. |
| `command` | string | `""` | shell | Command line (supports `{{VAR}}`). |
| `working_dir` | string | `"{{PROJECT_PATH}}"` | shell | Working directory. Defaults to the project root. |
| `is_background` | bool | `false` | shell | Long-running process (stays tracked). |
| `folder` | string | `""` (project root) | both | Target subfolder. Nestable with `/` (e.g. `"Callbacks/Shopee"`). |
| `hidden` | bool | `false` | both | Hidden from the tree by default (toggled back on with "Show hidden"). |
| `hide_on_run` | bool | `false` | both | Hides the Kai window when this command fires. |
| `capture_env` | bool | `false` | both | When used as a hook, captures the resulting environment and injects it as dynamic variables (e.g. a `gh auth`-style login that exports a token). |
| `open_last_link` | bool | `false` | shell | Opens the last `http(s)://` URL printed in the output in the browser on success. |
| `interactive_terminal` | bool | `false` | shell | Renders the output as a real terminal (grid of cells via libvterm) instead of a plain-text ANSI parser — needed for full-screen apps (vim, htop, a nested Claude Code). |
| `terminal_target` | string | `""` | shell | Name of a terminal target (Settings → Terminal Targets) that wraps the command, e.g. to run it inside WSL. |
| `compact_output` | bool | `false` | both | Collapses repeated blank lines and trims trailing whitespace in the output. |
| `ignore_exit_code` | bool | `false` | shell | Always treats the command as successful, regardless of its exit code — useful for tools that return non-zero even on success (e.g. `explorer.exe` opening a folder from WSL). |
| `auto_run` | bool | `false` | both | Fires automatically on Kai's startup, after `auto_run_delay_sec` seconds. |
| `auto_run_delay_sec` | number | `0` | both | Delay before an `auto_run` command fires. |
| `params` | array of parameter | `[]` | both | Form filled in before running. **The key is `params`, not `parameters`.** |
| `http_config` | object | — | http | Request configuration (see §5). |
| `responders` | array of responder | `[]` | shell | Auto-responders that answer output prompts for you (see §7). |
| `execution_conditions` | array of condition | `[]` | both | Guards that decide whether the command runs at all (see §8). |
| `condition_combinator` | `"and"` \| `"or"` | `"and"` | both | How `execution_conditions` combine. |
| `condition_skip_behavior` | `"success"` \| `"failure"` | `"success"` | both | What happens when the guard doesn't pass. |
| `hooks` | object `{pre, post, cleanup}` | — | both | Other commands to run before/after/on cleanup (see §9). |

> ⚠️ **Real gotcha:** the importer reads `params`. A `kai.json` using
> `parameters` will have its parameters **silently ignored**. Always use
> `params`.

### Simple shell example

```json
{
  "name": "Bring up the local stack",
  "type": "shell",
  "command": "docker compose up -d && echo \"Ready at http://localhost:{{PORT}}\"",
  "is_background": false
}
```

### Nested subfolder example

```json
{
  "name": "Deploy Shopee",
  "type": "shell",
  "folder": "Integrations/Shopee",
  "command": "./scripts/deploy.sh shopee"
}
```

### Every simple flag, one example each

```json
{ "name": "Internal helper", "type": "shell", "command": "./helper.sh", "hidden": true }
```
`hidden` — stays out of the tree until "Show hidden" is toggled on.

```json
{ "name": "Open editor", "type": "shell", "command": "code .", "hide_on_run": true }
```
`hide_on_run` — hides the Kai window while this command runs.

```json
{ "name": "gh auth login", "type": "shell", "command": "gh auth login", "capture_env": true }
```
`capture_env` — when used as a hook, captures the environment it exports
as dynamic variables (a login that exports a token, for example).

```json
{ "name": "Start tunnel", "type": "shell", "command": "ngrok http 3000", "open_last_link": true }
```
`open_last_link` — opens the last `http(s)://` URL printed in the output
once the command succeeds.

```json
{ "name": "htop", "type": "shell", "command": "htop", "interactive_terminal": true }
```
`interactive_terminal` — renders a real terminal grid instead of a
plain-text log, for full-screen apps.

```json
{ "name": "List files (WSL)", "type": "shell", "command": "ls -la", "terminal_target": "WSL" }
```
`terminal_target` — runs the command through a named Terminal Target
(Settings → Terminal Targets), e.g. inside WSL.

```json
{ "name": "Verbose build", "type": "shell", "command": "make -j4", "compact_output": true }
```
`compact_output` — collapses repeated blank lines and trims trailing
whitespace.

```json
{ "name": "Open folder", "type": "shell", "command": "explorer.exe .", "ignore_exit_code": true }
```
`ignore_exit_code` — always treats the command as successful, for tools
that return a non-zero exit code on success (e.g. `explorer.exe` from
WSL).

```json
{
  "name": "Start background sync", "type": "shell", "command": "./sync.sh",
  "is_background": true, "auto_run": true, "auto_run_delay_sec": 5
}
```
`auto_run` / `auto_run_delay_sec` — fires automatically `auto_run_delay_sec`
seconds after Kai starts.

---

## 3. Variables `{{VAR}}`

`command`, `working_dir`, `http_config.url`, headers and body all support
`{{VAR}}` interpolation.

### Precedence (rightmost wins)

```
Global (active Environment)  <  Folder/Project (inherited from parents)  <  Dynamic (extracted via HTTP)  <  Parameters (form)
```

- **Folder inheritance:** a command in a subfolder sees the `env_vars` of
  the project and every ancestor folder; the most specific one wins.
- **Missing variable** becomes an **empty string** + a log warning. Kai
  **never** crashes over a missing variable.
- Accepted placeholder names: letters, digits, `_` and `.` (the dot is used
  by collection fields — see §6).

### Always-available variables

| Variable | Source |
|---|---|
| `{{PROJECT_PATH}}` | Injected automatically = absolute path of the imported folder. |
| Any `env_vars` key | From the `kai.json` itself. |

### Dynamic / faker variables (resolved at run time)

`$` prefix inside the placeholder:

| Token | Result |
|---|---|
| `{{$uuid}}` | UUID v4 (with hyphens) |
| `{{$randomUuidHex}}` | UUID as 32 hex chars, no separators |
| `{{$timestamp}}` | epoch in **seconds** |
| `{{$timestampMs}}` | epoch in **milliseconds** |
| `{{$isoTimestamp}}` | ISO-8601 date/time in UTC |
| `{{$randomInt}}` | integer in `[0, 100000)` |
| `{{$randomInt.N}}` | integer in `[0, N)` (e.g. `{{$randomInt.500}}`) |

An unknown dynamic token falls back to an empty string + a warning (safe
fallback, never a crash).

```json
{
  "name": "New test order",
  "type": "shell",
  "command": "curl -X POST localhost:{{PORT}}/orders -d '{\"id\":\"{{$uuid}}\",\"ts\":{{$timestamp}},\"n\":{{$randomInt.500}}}'"
}
```

---

## 4. Parameters (form shown before running)

Each item in `params`:

| Key | Type | Description |
|---|---|---|
| `name` | string | Identifier. Becomes `{{name}}` in the command. Should be a valid identifier (letters/digits/`_`, starting with a letter or `_`) if you want to export it as a shell env var. |
| `label` | string | Label shown in the form. |
| `type` | see table below | Field type. |
| `default` | string | Initial value (may contain `{{VAR}}`). |
| `options` | array of string | Fixed options (only for a `select` without a collection). |
| `multi_select` | bool | Only for `select` with fixed `options`: turns it into a checkbox list; joins the checked values with a comma. |
| `collection` | string | Name of a collection in the same `kai.json` — ties the `select` to it (see §6). |
| `collection_display_field` | string | Schema field shown in the picker (e.g. `"value"`). |
| `initial_dir` | string | Only for `file`: starting folder for the file picker. Not read by the project-manifest importer today — only by the native command editor. |

### 4.1 Parameter types

| `type` | Widget | Value produced |
|---|---|---|
| `text` | Text field | The typed text. |
| `number` | Integer spinner | An integer (range `-1e9..1e9`), as a string. |
| `bool` | Checkbox | `"true"` or `"false"` (string). |
| `select` | Searchable combo / list / collection picker | The chosen value (see below). |
| `file` | Field + "..." button (native dialog) | Absolute path of the chosen file. |

### 4.2 Text

```json
{ "name": "MESSAGE", "label": "Message", "type": "text", "default": "hi" }
```
Used as: `echo "{{MESSAGE}}"`.

### 4.3 Number

```json
{ "name": "REPLICAS", "label": "Replicas", "type": "number", "default": "3" }
```
Produces an integer. Used as: `kubectl scale --replicas={{REPLICAS}} ...`.

### 4.4 Boolean

```json
{ "name": "DROP_DB", "label": "Recreate from scratch?", "type": "bool", "default": "false" }
```
Produces `"true"`/`"false"`. Typical use:
`if [ "{{DROP_DB}}" = "true" ]; then npm run db:reset; fi`.

### 4.5 Select

Three modes:

**(a) Fixed options (single choice):**
```json
{
  "name": "ENV_TARGET", "label": "Environment", "type": "select",
  "default": "dev", "options": ["dev", "staging", "production"]
}
```

**(b) Fixed options, multi-select** (`multi_select`): joins the checked
options with a comma.
```json
{
  "name": "SERVICES", "label": "Services", "type": "select",
  "multi_select": true, "options": ["api", "worker", "web", "cron"]
}
```
Result in `{{SERVICES}}`: e.g. `api,worker`.

**(c) Tied to a collection** (`collection`): see §6. Choosing an entry
injects **all its fields** as `{{name.field}}`.

### 4.6 File picker (`type: "file"`)

```json
{
  "name": "CERT", "label": "Certificate", "type": "file",
  "initial_dir": "{{PROJECT_PATH}}/certs"
}
```

- `initial_dir` decides where the picker **opens**. Without it, Qt reopens
  the last directory the process visited (Kai's own install folder, the
  first time). Only used if the path **exists**.
- **Note:** in the parameter dialog, `initial_dir` is used **literally** —
  `{{PROJECT_PATH}}` and other variables are **not** resolved there. Prefer
  a real absolute path if you need to guarantee it opens in the right place.
- The value produced is the **absolute path** of the chosen file.

---

## 5. HTTP commands

`type: "http"` with an `http_config` object:

| Key (inside `http_config`) | Type | Description |
|---|---|---|
| `method` | `GET`\|`POST`\|`PUT`\|`PATCH`\|`DELETE` | Method. |
| `url` | string | URL (supports `{{VAR}}`). |
| `headers` | object `{string:string}` | Headers (values support `{{VAR}}`). |
| `body` | string | Body (supports `{{VAR}}` and dynamic tokens). |
| `env_extractors` | array `{json_path, env_var}` | Extracts a field from the JSON response into a **dynamic variable**, available to the next commands/hooks. |

```json
{
  "name": "Authenticate",
  "type": "http",
  "http_config": {
    "method": "POST",
    "url": "{{BASE_URL}}/auth/login",
    "headers": { "Content-Type": "application/json" },
    "body": "{\"user\":\"{{USER}}\",\"pass\":\"{{PASS}}\"}",
    "env_extractors": [
      { "json_path": "token", "env_var": "AUTH_TOKEN" }
    ]
  }
}
```

Afterwards, another command can use `Authorization: Bearer {{AUTH_TOKEN}}` —
the extracted dynamic variable takes precedence over global/folder scope.

---

## 6. Collections (tabular data sources)

A **collection** is a **non-executable** item: a table of records with a
field schema. It serves as the source of a `select` parameter. Persisted
separately (`collections.json`), but it can be **versioned inside
`kai.json`**.

Each item in `collections`:

| Key | Type | Description |
|---|---|---|
| `name` | string | Collection name (a `param.collection` references it by this). |
| `icon` | string | Icon (e.g. `database`). |
| `folder` | string | Target subfolder (nestable with `/`). |
| `schema` | array of field | Columns. If omitted, uses the default `[key, value]` schema. |
| `entries` | array of entry | Rows. |

### Schema field

| Key | Type | Description |
|---|---|---|
| `name` | string | Stable key, used in `{{collection.<name>}}` and in `entries`. |
| `label` | string | Label shown in the grid. |
| `type` | `text`\|`key`\|`value`\|`email`\|`number`\|`url`\|`bool` | Type (validation/UI). Unknown type → `text`. |
| `visible` | bool | Whether it shows as a column in the grid (default `true`). Invisible fields stay editable. |

### Entry

| Key | Type | Description |
|---|---|---|
| `values` | object `{field:value}` | Maps a field's `name` to its value. |
| `favorite` | bool | Marks it as a favorite (default `false`). |

> The legacy `tags` key in entries is **ignored** (removed feature) — it
> may show up in old examples, but does nothing.

### Tying a `select` to a collection

The parameter references the collection **by name** (`collection`); Kai
resolves it to the id generated on import. Choosing an entry injects **all
of its fields** as `{{param.field}}`:

- `{{param.<field>}}` → the field's value from the **first** chosen entry.
- `{{param.<field>__all}}` → CSV of the field across **all** chosen entries
  (only with multi-select).

### Full example (collection + select + injection)

```json
{
  "project_name": "Local CRM",
  "env_vars": { "SMTP": "localhost:1025" },
  "commands": [
    {
      "name": "Greet Customer",
      "type": "shell",
      "command": "echo \"Hi, {{customer.value}} <{{customer.email}}> (id {{customer.key}})\"",
      "params": [
        {
          "name": "customer",
          "label": "Customer",
          "type": "select",
          "collection": "Customers",
          "collection_display_field": "value"
        }
      ]
    }
  ],
  "collections": [
    {
      "name": "Customers",
      "icon": "database",
      "schema": [
        { "name": "key",   "label": "ID",    "type": "key" },
        { "name": "value", "label": "Name",  "type": "value" },
        { "name": "email", "label": "Email", "type": "email" }
      ],
      "entries": [
        { "values": { "key": "1", "value": "Alice", "email": "alice@example.com" }, "favorite": true },
        { "values": { "key": "2", "value": "Bob",   "email": "bob@example.com" } },
        { "values": { "key": "3", "value": "Carol", "email": "carol@example.com" } }
      ]
    }
  ]
}
```

Running "Greet Customer" shows the collection picker; choosing "Alice"
turns the command into: `echo "Hi, Alice <alice@example.com> (id 1)"`.

---

## 7. Auto-responders

An entry in `responders` watches the command's output and answers a prompt
for you — useful for interactive scripts that ask `[y/N]`-style questions.

| Key | Type | Description |
|---|---|---|
| `name` | string | Identifier. **Required** — without it, the responder is discarded. |
| `enabled` | bool | Whether it's active (default `true`). |
| `pattern` | string | Regex matched against the output; a match fires the response. Captured groups are available as `\1`, `\2`, ... in `response`. |
| `response` | string | Text sent to the process's stdin, as if typed. |
| `limit_triggers` | bool | If on, fires at most `max_triggers` times per run. |
| `max_triggers` | number | Cap when `limit_triggers` is on (default `1`). |

```json
{
  "responders": [
    { "name": "Confirm", "pattern": "\\[y/N\\]", "response": "y" }
  ]
}
```

---

## 8. Execution conditions

A guard that decides whether a command — main or hook — actually runs, based
on comparing two interpolated values. Motivating case: "only run the login
hook if TOKEN is empty, or EXPIRES_AT is in the past".

Each item in `execution_conditions`:

| Key | Type | Description |
|---|---|---|
| `name` | string | Optional label — shown in skip/failure log messages, useful for debugging which condition decided the result. Without one, falls back to an auto-generated summary. |
| `left` | string | Interpolated text (`{{VAR}}`, dynamic tokens like `{{$timestamp}}`, or a literal). |
| `op` | see table | Comparison operator. |
| `right` | string | Interpolated text, compared against `left`. Ignored for `exists`/`not_exists`. |

| `op` | Meaning |
|---|---|
| `exists` | `left` (interpolated) is not empty. |
| `not_exists` | `left` (interpolated) is empty. |
| `eq` / `ne` | equal / not equal — numeric if both sides parse as a number, string otherwise. |
| `gt` / `ge` / `lt` / `le` | greater than / greater or equal / less than / less or equal — numeric only; falls back to `false` on non-numeric values. |
| `contains` / `not_contains` | substring check. |

`condition_combinator` (`"and"` default, or `"or"`) decides how multiple
rows combine. `condition_skip_behavior` decides what a failed guard means:
`"success"` (default — skip quietly, the pipeline moves on) or `"failure"`
(skip and fail the pipeline, like any other error).

```json
{
  "name": "Login",
  "type": "shell",
  "command": "gh auth login",
  "execution_conditions": [
    { "name": "Token missing", "left": "{{TOKEN}}", "op": "not_exists" },
    { "name": "Token expired", "left": "{{EXPIRES_AT}}", "op": "lt", "right": "{{$timestamp}}" }
  ],
  "condition_combinator": "or"
}
```

---

## 9. Hooks

`hooks` runs other commands before, after, or on cleanup:

```json
{
  "hooks": {
    "pre": ["Login"],
    "post": ["Notify"],
    "cleanup": []
  }
}
```

Unlike most references in this file, hook entries are the referenced
command's **name**, not an id — the manifest doesn't know the ids Kai
generates on import. A name resolved against no command in the same
`kai.json` is ignored (logged, never breaks the import).

- **pre**: run before the main command. If one fails (exit code != 0, or
  HTTP >= 400), the whole pipeline aborts and the main command never runs.
- **post**: run after the main command succeeds. If one fails, the pipeline
  is marked as failed.
- **cleanup**: always run when execution ends — success, failure, manual
  stop, or reset — to tear down whatever the command brought up.

---

## 10. Generic detection (without a `kai.json`)

Turning on **"Detect generic definitions"** in the Import Project dialog
makes Kai recognize, in the same folder:

| Ecosystem | Detected from | Produces |
|---|---|---|
| npm/Node | `package.json` `scripts` | One command per script (`npm run <name>`, or `yarn`/`pnpm` if that lockfile is present). |
| Docker Compose | `docker-compose.yml`/`compose.yaml` | "Bring everything up", "Stop everything", and a "View logs" command per service. |
| Python | `requirements.txt`/`pyproject.toml`/`manage.py` | Install-dependency commands and, for Django (`manage.py`), `runserver`/`migrate`. |
| PHP/Composer | `composer.json` | `composer install` plus one command per script. |
| Makefile | `Makefile`/`GNUmakefile` | One command per target. |

Each ecosystem's commands land in their own subfolder, never mixed with a
`kai.json`'s own commands — this works even when there's no `kai.json` at
all, in which case the project name comes from the folder name.

---

## 11. Folders and organization

There's no `folders` section in a project `kai.json`. The tree is built like
this:

- The **root folder** = `project_name`.
- **Subfolders** are created on demand from each command's/collection's
  `folder` key. Nest with `/` (e.g. `"A/B/C"` creates three levels).
  Appearance order is preserved.

```json
{
  "project_name": "Monorepo",
  "commands": [
    { "name": "Web dev",    "type": "shell", "folder": "Frontend",       "command": "pnpm --filter web dev", "is_background": true },
    { "name": "API dev",    "type": "shell", "folder": "Backend/API",    "command": "go run ./cmd/api" },
    { "name": "Worker dev", "type": "shell", "folder": "Backend/Worker", "command": "go run ./cmd/worker" }
  ]
}
```

---

## 12. Full reference model (copy and adapt)

Covers every parameter type, HTTP with extraction, a collection, subfolders
and dynamic variables.

```json
{
  "project_name": "E-Commerce Microservice",
  "icon": "shopping-cart",
  "env_vars": {
    "PORT": "8080",
    "NODE_ENV": "development",
    "BASE_URL": "http://localhost:8080"
  },
  "commands": [
    {
      "name": "Dev Server",
      "type": "shell",
      "command": "npm run dev",
      "working_dir": "{{PROJECT_PATH}}",
      "is_background": true
    },
    {
      "name": "Migrations",
      "type": "shell",
      "folder": "Database",
      "command": "if [ \"{{DROP}}\" = \"true\" ]; then npm run db:reset; else npm run db:migrate; fi",
      "params": [
        { "name": "DROP", "label": "Recreate from scratch?", "type": "bool", "default": "false" }
      ]
    },
    {
      "name": "Scale service",
      "type": "shell",
      "folder": "Ops",
      "command": "kubectl scale deploy/{{SVC}} --replicas={{N}}",
      "params": [
        { "name": "SVC", "label": "Service", "type": "select", "options": ["api", "worker", "web"], "default": "api" },
        { "name": "N",   "label": "Replicas", "type": "number", "default": "2" }
      ]
    },
    {
      "name": "Authenticate",
      "type": "http",
      "folder": "API",
      "http_config": {
        "method": "POST",
        "url": "{{BASE_URL}}/auth/login",
        "headers": { "Content-Type": "application/json" },
        "body": "{\"user\":\"{{USER}}\",\"pass\":\"{{PASS}}\",\"nonce\":\"{{$uuid}}\"}",
        "env_extractors": [ { "json_path": "token", "env_var": "AUTH_TOKEN" } ]
      },
      "params": [
        { "name": "USER", "label": "User", "type": "text", "default": "admin" },
        { "name": "PASS", "label": "Password", "type": "text" }
      ]
    },
    {
      "name": "List user",
      "type": "http",
      "folder": "API",
      "http_config": {
        "method": "GET",
        "url": "{{BASE_URL}}/users/{{target.key}}",
        "headers": { "Authorization": "Bearer {{AUTH_TOKEN}}" }
      },
      "params": [
        { "name": "target", "label": "User", "type": "select", "collection": "Users", "collection_display_field": "value" }
      ]
    }
  ],
  "collections": [
    {
      "name": "Users",
      "icon": "database",
      "folder": "API",
      "schema": [
        { "name": "key",   "label": "ID",    "type": "key" },
        { "name": "value", "label": "Name",  "type": "value" },
        { "name": "email", "label": "Email", "type": "email" }
      ],
      "entries": [
        { "values": { "key": "10", "value": "Alice", "email": "alice@example.com" }, "favorite": true },
        { "values": { "key": "11", "value": "Bob",   "email": "bob@example.com" } }
      ]
    }
  ]
}
```

---

## 13. Checklist for an AI generator

When producing a `kai.json`, make sure:

- [ ] Use `params` (never `parameters`).
- [ ] Command `type` ∈ {`shell`, `http`}; HTTP always with `http_config`.
- [ ] Parameter `type` ∈ {`text`, `number`, `bool`, `select`, `file`}.
- [ ] A `select` tied to a collection uses `collection` (name) + `collection_display_field`.
- [ ] `{{VAR}}` references match `env_vars`, params, `{{PROJECT_PATH}}`, or `{{$...}}` dynamic tokens.
- [ ] Collection fields referenced as `{{param.field}}` match the `schema`.
- [ ] `folder` uses `/` to nest; related commands are grouped together.
- [ ] `working_dir` only set when different from the root (the default is already `{{PROJECT_PATH}}`).
- [ ] `hooks` entries reference other commands **by name**, and only names that exist in the same file.
- [ ] Valid JSON (quotes escaped inside `command`/`body`).

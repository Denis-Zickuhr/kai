# Spec 02: Data Models & Storage

## 1. Data location
- **Linux:** `~/.config/kai/`
- **Windows:** `%APPDATA%/Kai/`

## 2. Commands and folders schema (`commands.json`)

Every folder with `parent_id: null` renders as a dynamic tab, using its
`icon`. Subfolders stay nested in that root tab's tree. Importing a
`kai.json` creates a normal folder: `project_path` may preserve the
import's origin for use in `working_dir`/`PROJECT_PATH`, but doesn't create
a special category or tab.

```json
{
  "folders": [
    {
      "id": "f_global_1",
      "name": "System Tools",
      "icon": "terminal-icon",
      "parent_id": null,
      "project_path": null,
      "env_vars": { "GLOBAL_TOOL_VER": "1.0.0" }
    },
    {
      "id": "f_proj_ecommerce",
      "name": "E-Commerce Microservice",
      "icon": "shopping-cart",
      "parent_id": null,
      "project_path": "/home/user/projects/ecommerce-api",
      "env_vars": {
        "PORT": "8080",
        "DATABASE_URL": "postgres://localhost:5432/ecommerce_dev"
      }
    }
  ],
  "commands": [
    {
      "id": "c_shell_simple",
      "folder_id": "f_proj_ecommerce",
      "name": "Build Release",
      "type": "shell",
      "icon": "build",
      "command": "cargo build --release",
      "working_dir": "{{PROJECT_PATH}}",
      "is_background": false,
      "params": [],
      "hooks": { "pre": [], "post": [], "cleanup": [] }
    },
    {
      "id": "c_shell_param",
      "folder_id": "f_proj_ecommerce",
      "name": "Run Migration",
      "type": "shell",
      "command": "db-cli migrate --env {{ENV_TARGET}} --drop-first={{DROP_DB}} --config {{CONFIG_FILE}}",
      "working_dir": "{{PROJECT_PATH}}",
      "params": [
        { "name": "ENV_TARGET", "label": "Target Environment", "type": "select", "default": "dev", "options": ["dev", "staging", "production"] },
        { "name": "DROP_DB", "label": "Recreate DB from scratch?", "type": "bool", "default": false },
        { "name": "CONFIG_FILE", "label": "Config file", "type": "file", "default": "./config/db.json" }
      ],
      "hooks": { "pre": [], "post": [], "cleanup": [] }
    },
    {
      "id": "c_shell_bg",
      "folder_id": "f_proj_ecommerce",
      "name": "Start Dev Server",
      "type": "shell",
      "command": "npm run dev",
      "working_dir": "{{PROJECT_PATH}}",
      "is_background": true,
      "hooks": { "pre": ["c_http_login"], "post": [], "cleanup": [] }
    },
    {
      "id": "c_http_login",
      "folder_id": "f_proj_ecommerce",
      "name": "API - Authenticate Admin",
      "type": "http",
      "http_config": {
        "method": "POST",
        "url": "http://localhost:{{PORT}}/api/v1/auth/login",
        "headers": { "Content-Type": "application/json" },
        "body": "{\"username\":\"admin\",\"password\":\"secret\"}",
        "env_extractors": [ { "json_path": "data.token", "env_var": "AUTH_TOKEN" } ]
      },
      "hooks": { "pre": [], "post": [], "cleanup": [] }
    },
    {
      "id": "c_http_get_users",
      "folder_id": "f_proj_ecommerce",
      "name": "API - List Users",
      "type": "http",
      "http_config": {
        "method": "GET",
        "url": "http://localhost:{{PORT}}/api/v1/users",
        "headers": { "Authorization": "Bearer {{AUTH_TOKEN}}", "Accept": "application/json" }
      },
      "hooks": { "pre": ["c_http_login"], "post": [], "cleanup": [] }
    }
  ]
}
```

`Command` also carries: `description`, `hidden`, `hide_on_run`,
`capture_env`, `open_last_link`, `interactive_terminal`, `terminal_target`,
`compact_output`, `ignore_exit_code`, `auto_run`/`auto_run_delay_sec`,
`responders` (auto-responders), and `execution_conditions`/
`condition_combinator`/`condition_skip_behavior` — see the [`kai.json`
manifesto](../docs/manifesto/kai-json-manifesto.md) for the full field
list and semantics.

## 3. Importable project spec (`kai.json`)

Located at the root of a user's repository:
```json
{
  "project_name": "E-Commerce Microservice",
  "icon": "shopping-cart",
  "env_vars": { "PORT": "8080", "NODE_ENV": "development" },
  "commands": [
    { "name": "Dev Server", "type": "shell", "command": "npm run dev", "is_background": true },
    { "name": "Run Tests", "type": "shell", "command": "npm test" }
  ]
}
```

## 4. Global settings & themes schema (`settings.json`)
```json
{
  "active_theme": "dracula",
  "global_hotkey": "Ctrl+Shift+B",
  "start_visible": true,
  "window_mode": "size",
  "edit_item_shortcut": "F2",
  "delete_item_shortcut": "Delete",
  "new_folder_shortcut": "Ctrl+Shift+N",
  "new_command_shortcut": "Ctrl+N",
  "quit_app_shortcut": "Ctrl+Q",
  "toggle_search_shortcut": "Ctrl+F",
  "command_creation_mode": "standard",
  "command_edit_mode": "standard",

  "global_env_vars": {
    "USER_HOME": "/home/user",
    "DEFAULT_SHELL": "/bin/zsh"
  },

  "environments": [
    { "id": "env_global", "name": "Global", "vars": { "USER_HOME": "/home/user", "DEFAULT_SHELL": "/bin/zsh" } },
    { "id": "env_prod", "name": "Prod", "vars": { "BASE_URL": "https://api.prod.com" } }
  ],
  "active_environment_id": "env_global"
}
```

### 4.1. Environments (selectable variable packages)

"Global" variables are organized into **environments** — named packages of
variables (Insomnia/Postman vibe). Only one package is **active** at a
time (`active_environment_id`) and feeds the *Global* scope of
`EnvironmentManager` (the precedence hierarchy doesn't change:
`Global < Folder < Dynamic < Parameters`).

- **Picker at the top-right** (title bar): a combo box switches the active
  package on the fly; a button next to it opens the **management screen**
  (`EnvironmentManagerDialog`) to create/duplicate/rename/delete packages
  and edit their variables.
- **Smooth migration:** loading a legacy `settings.json` that only has
  `global_env_vars` and no `environments` automatically creates a
  `"Global"` package with those variables and marks it active — nothing is
  lost. `global_env_vars` is kept only as a migration artifact; editing
  happens through environments from then on.
- **Safe fallback:** if `active_environment_id` points at a package that no
  longer exists, the first package in the list becomes active.

### 4.2. Startup visibility (`start_visible`)

Decides whether the window shows up when Kai starts, **without depending
on the global shortcut**: on, always visible; off, always hidden (tray
only). It's binary on purpose — it used to fall back to "visible if the
shortcut fails to register", which reintroduced the exact problem the
setting exists to solve (shortcut registration can fail for reasons
outside the user's control, e.g. a WSL/WSLg environment, or a collision
with another app already using the same combination). Reopening with it
off is still possible via the tray icon or `kai show` (CLI/IPC).

### 4.3. Run history (`runs.json`)

Every command run (Shell or HTTP) is recorded in `runs.json` (same config
directory), with `command_id`/`command_name`/`command_type`,
`started_at` (ISO), `duration_ms`, `success` and `output` (accumulated
output, truncated). Keeps at most 200 records, most recent first; output
per run is capped. The **Run History** screen (Processes menu) lists them,
inspects the output, and lets you **re-run** or **clear** the history.

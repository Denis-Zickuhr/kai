# Kai

A native command runner for developers, written in C++20 with Qt 6.

Kai keeps the commands you run every day — shell scripts, HTTP requests,
multi-step tasks — one global shortcut away. It injects variables based on
the active project, streams output live, and never leaves orphaned
processes behind.

**Status: beta.** It works and is used daily, but the UI and config file
format may still change between versions.

---

## Features

### Execution

| Type | What it does |
|---|---|
| **Shell** | Runs commands and scripts, with incremental output and ANSI colors. Supports long-running background processes and a real interactive terminal (TUI) for full-screen apps like vim, htop, or a nested Claude Code. |
| **HTTP** | REST requests with method, headers, body and query. Response with a navigable JSON tree, headers, timing and size, plus a "Request" tab showing exactly what was sent. |

- **Tracked processes:** lists what's running with its PID, lets you attach
  to the output or kill it. Background commands run in their own process
  group, so stopping one never leaves an orphaned child behind.
- **Pre, post and cleanup hooks:** `cleanup` always runs when execution
  ends — success, failure, manual stop or reset — to tear down whatever the
  command brought up.
- **Execution conditions:** gate whether a command (or hook) actually runs,
  based on a comparison of interpolated values (env vars, `{{$now}}`-style
  dynamic tokens, literals). Combine multiple conditions with AND/OR; when a
  guard doesn't pass, treat it as success or as a real failure.
- **Terminal targets:** choose the shell that runs the command. Run it in
  WSL, inside a container, or through any wrapper, via a template with the
  command passed as base64 (immune to quoting issues).
- **Per-command timeout**, propagated exit code, and an option to ignore the
  exit code entirely for tools that report a non-zero code even on success
  (e.g. `explorer.exe` opening a folder from WSL).

### Variables

- **Hierarchy:** global → folder/project → extracted from HTTP → form
  parameters. The most specific one wins, and a missing key returns an
  empty string with a log warning instead of breaking execution.
- **Environments:** selectable variable packages at the top of the window,
  Insomnia/Postman style. Variables can be marked as secret, in which case
  the value never shows up in the command line recorded in the log.
- **Dynamic:** `$uuid`, `$timestamp`, `$randomInt` and similar, resolved at
  execution time.
- **Response extraction:** pulls a field out of a request's JSON into a
  variable — authenticate, capture the token, use it in the next command.

### Input

- **Parameters:** a form filled in before running, with text, select, bool
  and file fields. The file picker accepts a configurable starting folder.
- **Collections:** your own tabular data sources (customers, environments,
  whatever you need), usable as the source of a select parameter.
- **Import:** a pasted `curl` command, an OpenAPI/Swagger spec in JSON, or a
  `kai.json` at the root of a project — including automatic detection of
  npm, Docker Compose, Python and Composer/Makefile projects that don't
  have one yet.

### Interface

- Configurable global shortcut to bring the window up from anywhere.
- Fuzzy search by command name.
- A tree of folders and commands, with tabs and keyboard navigation.
- Output panel with tabs (response, output, headers, variables, raw),
  line numbers, line wrap, timestamps and a compact mode.
- History of past runs.
- 6 JSON themes with reload on save, plus adjustable density and corner
  style.
- Portuguese and English, including the built-in help.
- Option to hide the window while a command runs, and to hide it on focus
  loss.

### Command line

Talks to the running instance over a local socket:

```bash
kai run <command>     # run by name
kai list              # list available commands
kai env list           # list environments
kai env use <name>    # activate an environment
kai ps                 # list running processes
kai attach <id>        # follow a process's output
kai kill <id>          # stop a process
kai show               # bring the Kai window to front
```

---

## Platforms

| Platform | Status |
|---|---|
| **Linux** | Developed and tested here. X11 and Wayland (via XWayland). |
| **Windows** | Cross-compiled and tested manually. Installer and portable build. |

macOS isn't supported: there's no code or build for it.

---

## Building

**Requirements:** CMake 3.16+, a C++20 compiler, Qt 6 (Core, Gui, Widgets,
Network, Svg). QHotkey and libvterm are fetched automatically by CMake.

```bash
./build.sh              # builds to build/bin/kai
./build.sh install      # installs to ~/.local/bin, assets to ~/.local/share/kai
```

`install` copies the themes, language packs and help content next to the
binary — they live on disk, not embedded in the executable.

Windows builds run in a container; see [`BUILD-WINDOWS.md`](BUILD-WINDOWS.md):

```bash
docker compose run --rm build-linux    # AppImage
docker compose run --rm build-windows  # portable folder (+ installer, opt-in)
```

### Tests

```bash
cd build && ctest
```

50+ suites covering models, variable resolution, the execution pipeline,
import parsers, UI components and language pack parity. Without a display,
use `QT_QPA_PLATFORM=offscreen`.

---

## The `kai.json` file

Placed at the root of a project, it's imported automatically along with its
commands and variables:

```json
{
  "project_name": "E-Commerce Microservice",
  "icon": "shopping-cart",
  "env_vars": {
    "PORT": "8080",
    "NODE_ENV": "development"
  },
  "commands": [
    {
      "name": "Dev Server",
      "type": "shell",
      "command": "npm run dev",
      "is_background": true
    },
    {
      "name": "Migrations",
      "type": "shell",
      "command": "npm run db:migrate"
    }
  ]
}
```

A full guide for generating one (by hand or with an AI model) lives in
[`docs/manifesto/kai-json-manifesto.md`](docs/manifesto/kai-json-manifesto.md).
There's also a ready-made sample project in [`sample/`](sample/README.md).

---

## Documentation

The [`docs/`](docs/README.md) folder has a per-topic guide with examples.
The same content is in the built-in help, under the **Help** menu, with
search.

Getting started: [overview](docs/overview.md) ·
[shell commands](docs/commands-shell.md) · [HTTP commands](docs/commands-http.md) ·
[variables](docs/variables.md) · [environments](docs/environments.md) ·
[hooks](docs/hooks.md) · [CLI](docs/cli.md)

Import: [cURL](docs/import-curl.md) · [OpenAPI](docs/import-openapi.md) ·
[kai.json](docs/kai-json.md)

Advanced: [terminal targets & WSL](docs/terminal-targets-wsl.md) ·
[collections & parameters](docs/collections-and-params.md) ·
[dynamic variables](docs/dynamic-vars.md) · [themes](docs/themes.md) ·
[shortcuts](docs/shortcuts.md)

Not yet implemented: [feature backlog](docs/roadmap-ideas.md).

---

## License

Proprietary software. All rights reserved — see [`LICENSE`](LICENSE).
Publishing the source doesn't grant a license to use it.

Kai uses third-party components under their own licenses: **Qt 6** (LGPLv3,
dynamically linked), **QHotkey** (BSD 3-Clause), **libvterm** (MIT) and
**Lucide Icons** (ISC and MIT). Copyright notices and full license texts
are in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md), which ships with
the distributed packages.

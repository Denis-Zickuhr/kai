# Kai

A native command runner and HTTP client for developers, written in C++20 with Qt 6.

Kai sits in the tray and is one global shortcut away. It holds the commands
you run every day — shell lines, Python/Node/PHP snippets, HTTP requests —
fills in the variables for the active project, streams the output live, and
cleans up after itself: stop a command and nothing it started is left running.

**Status: beta.** It is used daily, but the UI and the config format may still
change between versions.

---

## What it does

- **Commands.** A shell line, or code in Python, Node or PHP written directly
  in the command (no `python3 -c "..."` quoting). Long-running background
  commands, and a real interactive terminal for full-screen programs (vim,
  htop). Each command runs in its own process group, so Stop ends the whole
  tree.
- **HTTP.** Any method, headers/auth inherited from folders, JSON/XML/form/
  multipart/GraphQL bodies, cookies, assertions, value extractors, SSE, and
  "copy as cURL / Python / JavaScript". Imports cURL, OpenAPI, Postman,
  Insomnia and HAR.
- **Variables.** `{{NAME}}` resolves Global → Folder/Project → dynamic
  (`$uuid`, `$timestamp`…) → form parameters, the most specific winning. A
  missing name becomes an empty string and a log line, never an error.
  Environments are switchable sets; secret values never reach the log.
- **Around a command.** Pre/post/cleanup hooks (`cleanup` always runs),
  execution conditions, parameters with a form, schedules, auto-replies to
  prompts, and a per-command terminal target (WSL, a container, any wrapper).
- **KIP.** A command that speaks the Kai Interface Protocol gets a small UI —
  forms, progress, tables, diffs — drawn by Kai.
- **Projects as files.** A `kai.yml` at a project's root holds its commands
  and variables; import, export and sync are YAML and diffable.
- **CLI.** The same `kai` binary runs commands from a local `kai.yml`, or
  talks to the running app (`kai run`, `kai ps`, `kai kill`…).
- **AI assistant (optional, off by default).** Editor assist, a chat that can
  act with your approval, and an MCP bridge for external agents.

The window is a grid of panels you can rearrange, with JSON themes and a
Portuguese/English UI. Details for each feature are in [`docs/`](docs/README.md).

Kai is deliberately light: it never opens its window on its own, never blocks
the GUI thread, and doesn't touch your shell rc files or the network unless you
ask. The reasoning is in [`docs/philosophy.md`](docs/philosophy.md).

---

## Using it

A project is a folder with a `kai.yml`:

```yaml
project_name: "My Project"
env_vars:
  PORT: "8080"
commands:
  - name: "Dev server"
    type: "command"
    command: "npm run dev -- --port {{PORT}}"
    is_background: true
    cli_path: "dev"
  - name: "Health"
    type: "http"
    http_config:
      method: "GET"
      url: "http://localhost:{{PORT}}/health"
```

Import it from **File → Import Project** (Kai also detects npm, Docker
Compose, Python and Makefile projects that don't have one yet), then press
Enter on a command. From a terminal:

```bash
cd my-project
kai                 # list the commands that have a cli_path
kai dev             # run "Dev server" from the local kai.yml

kai run "Health"    # run a command of the running app, by name
kai ps              # processes it started
kai attach <id>     # follow one's output
kai kill <id>       # stop it
kai show            # bring the window up
```

Projects to try are in [`sample/`](sample/README.md). A guide for writing a
`kai.yml` by hand or with an AI model is under **Help → Creation manifesto**.

---

## How it's put together

Five static libraries and one executable. Dependencies point downward; the
lower layers never include UI headers.

| Library | Source | Role |
|---|---|---|
| `kai-core` | `src/core/` | Data model (`Command`, `Folder`, `Collection`…), config storage with corrupted-file recovery, variable resolution (`EnvironmentManager`), the KIP protocol parser, `kai.yml` import/export and validation |
| `kai-engine` | `src/engine/` | `ExecutionPipeline` (conditions, hooks, cleanup), `ProcessRunner` (PTY on Unix, ConPTY on Windows), `HttpRunner`, scheduler, project sync, AI agent tools |
| `kai-ui` | `src/ui/` | Qt Widgets: `MainWindow`, the dockable panel grid, output, editors, documents, settings |
| `kai-ipc` | `src/ipc/` | `QLocalServer` that keeps a single instance and serves the CLI (line-delimited JSON) |
| `kai-cli` | `src/cli/` | The command-line side: local paths, completion, MCP bridge |

`src/utils/` holds the theme and translation managers, the logger and the
path/duration helpers shared by all of them.

A run goes like this:

```
click / Enter / `kai run` / schedule
        │
   MainWindow ── asks for parameters ──► form
        │
   EnvironmentManager   resolves {{vars}}: Global → Folder → Dynamic → Params
        │
   ExecutionPipeline    conditions → pre hooks → main → post hooks → cleanup
        │                         │
   ProcessRunner            HttpRunner
   (PTY / ConPTY / QProcess) (QNetworkAccessManager)
        │
   signals ──► Output panel · run history · notifications · CLI streams
```

Rules the code follows: nothing blocks the GUI thread (file I/O, processes and
HTTP are asynchronous), and `core/` and `engine/` talk to the UI only through
Qt signals and slots. User-facing text goes through `utils::tr("key")`, with
the keys in both `assets/i18n/en.json` and `pt.json` (a test checks they match).

Large classes are split by responsibility across files named after the class:
`MainWindow` is `main-window.cpp` plus `main-window-<topic>.cpp`, and likewise
for `OutputPanel`, `DocViewer`, `CommandTreeWidget`, `ConfigManager`,
`ExecutionPipeline` and the KIP protocol.

---

## Building

**Requirements:** CMake 3.16+, a C++20 compiler, and Qt 6.4+ (Core, Gui,
Widgets, Network, Svg; Test for the suite). On Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-svg-dev \
                 libxkbcommon-dev
```

QHotkey and libvterm are downloaded by CMake on the first configure, so that
first build needs network access.

```bash
./build.sh              # Release build (app only, no tests) → build/bin/kai
./build.sh install      # → ~/.local/bin/kai, assets → ~/.local/share/kai
```

`install` also copies themes, language packs and help content — they live on
disk next to the binary, not inside the executable.

Tests (Qt Test, around 175 suites):

```bash
./build.sh tests        # app + test suite → build-dev/ (shared kai-* libs, small test binaries)
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -j4
```

Windows and AppImage builds run in containers — see
[`BUILD-WINDOWS.md`](BUILD-WINDOWS.md):

```bash
docker compose run --rm build-linux     # AppImage
docker compose run --rm build-windows   # portable folder (+ installer)
```

**Platforms:** Linux (X11, and Wayland through XWayland) and Windows. There is
no macOS build.

---

## Documentation

[`docs/`](docs/README.md) has one page per topic, and the same pages are in
the app under **Help**, with search. Good starting points:
[overview](docs/overview.md) · [commands](docs/commands.md) ·
[HTTP](docs/commands-http.md) · [variables](docs/variables.md) ·
[`kai.yml`](docs/kai-yml.md) · [CLI](docs/cli.md) · [KIP](docs/kip.md) ·
[AI assistant](docs/ai.md) · [backlog](docs/roadmap-ideas.md).

---

## License

Proprietary software, all rights reserved — see [`LICENSE`](LICENSE).
Publishing the source doesn't grant a license to use it.

Kai uses third-party components under their own licenses: **Qt 6** (LGPLv3,
dynamically linked), **QHotkey** (BSD 3-Clause), **libvterm** (MIT) and
**Lucide Icons** (ISC and MIT). Notices and license texts are in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md), which ships with the
packages.

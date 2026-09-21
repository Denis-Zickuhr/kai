# CLI — Kai in the terminal

The same `kai` binary works three ways:

| Mode | When | What it runs |
|------|------|--------------|
| **Local CLI Paths** | current directory has `kai.json` / `kai.yml` / `kai.yaml` | commands from **that file**, no running Kai needed |
| **Global CLI Paths** | no local file, or forced with `-g` / `--global` | commands with a `cli_path` registered in the app |
| **Instance control** | `run`, `list`, `env`, `ps`, `attach`, `kill`, `import`, `show` | talks to the running Kai over a local socket |

## CLI Paths

Only folders/commands with a **CLI Path** (`cli_path` in the file, or the
field in the folder/command editors) are reachable. Each folder with a path
is a segment; the last segment is always a command. Folders without one are
transparent — their children with a path surface at the nearest ancestor
that has one.

```yaml
folders:
  - path: Docker
    cli_path: docker
commands:
  - name: Start container
    folder: Docker
    cli_path: up
    type: command
    command: docker compose up -d {{service}}
    params:
      - name: service
        type: text
      - name: build
        type: bool
        optional: true
```

| Command | Action |
|---------|--------|
| `kai` | lists the root paths |
| `kai docker` | incomplete path: lists what's inside |
| `kai docker up api --build=true` | runs the command |
| `kai docker up --help` | usage, description and parameters; runs nothing |
| `kai -g docker up api` | forces the global (app) paths from inside a folder with a local file |
| `kai -d docker up api` | detached: starts it and returns immediately (global: registered in the app; local: background process, output to a log file) |
| `kai -gd docker up api` | both; short flags combine (`-dg` too), long forms `--global --detached` |
| `kai -n docker up api` | tray notification when it finishes (`-dn`/`-gdn` too — with `-gd` the app notifies) |
| `kai -gw docker up api` | also opens the command's output in its own window (long form `--window`; combines: `-gdw`). Needs `-g`: the window belongs to the app. It follows your window preferences (fixed size, maximized, fullscreen, or remember size) and mirrors the same tabs and theme as the docked output |
| `kai --dry-run docker up api` | prints the rendered command, directory, terminal target and hooks without running |
| `kai --json docker` | listing (and `--dry-run`) as JSON |

- Required parameters are **positional**, in schema order; optional ones
  are only `--name=value` and fall back to their default.
- `bool`, `number` and fixed-option `select` values are validated before
  running.
- Exit codes: `0` success; on failure, the command's **real** exit code (e.g. `127`, `130` on Ctrl+C; `1` when there is none, e.g. HTTP); `2` usage error.

### Local mode semantics

Local mode executes **only what the file defines**: the template is
rendered and run directly in the platform shell (bash on Linux, `cmd.exe`
on Windows — or, when `kai.exe` is called from a WSL folder, inside that
same distro via `wsl.exe`). Terminal targets and other app-side settings are ignored; the
only app input is the active Environment's variables. The command inherits
the terminal (interactive prompts, colors, Ctrl+C), so responders,
`open_last_link` and `compact_output` don't apply; it isn't registered in
the app. Exception: `kai.exe` called from WSL gets pipes, not a terminal, so
there the command runs under a ConPTY (the Linux side sees a tty) and the
keyboard is forwarded, with the duplicated echo removed.

**KIP commands** (`kip: true`, see [kip.md](kip.md)) are **refused** in local
mode: they need the app to draw their screens. Kai prints `KIP commands need
the Kai app — use kai -g` and exits with `1` before running anything.

### Global mode semantics

Global runs are **delegated to the app** over a long-lived IPC session
(`run-stream`, see `src/ipc/stream-channel.h`): the app runs the command
like a click in the tree (process list, history, output panel, terminal
targets, persisted dynamic vars) and streams output back; keystrokes go the
other way (raw tty mode on Unix, so Ctrl+C reaches the command). If Kai
isn't running, the CLI starts it hidden in the tray (`KAI_FORCE_GUI=1`,
`KAI_START_HIDDEN=1`) under a lock file, so concurrent calls never spawn a
second instance. Closing the terminal detaches; the command keeps running. A KIP command
runs the same way, and the app comes to the front on its KIP view (`-w`
opens that view in its own window); the terminal shows only the log lines
and gets the real exit code.
If the app can't be reached at all, the command runs in the terminal as a
fallback, unregistered.

## Instance control

| Command | Action |
|---------|--------|
| `kai run "Deploy"` | runs a command by **name**, inside the app (terminal target, dynamic vars, history, process list) |
| `kai list` | lists available commands |
| `kai env list` | lists environments |
| `kai env use Prod` | activates an environment |
| `kai ps [--json]` | lists running processes |
| `kai attach <pid\|name>` | streams an already-running process's output to the terminal (replays recent output, then live); keyboard goes to the process, `Ctrl+]` detaches |
| `kai history [N] [--json]` | latest runs recorded by Kai (reads runs.json, no running Kai needed) |
| `kai last [--log] [--json]` | the most recent run, optionally with its output |
| `kai init [--json] [--force]` | writes a `kai.yml` from the generic project detection (npm, compose, make, python...) with CLI paths |
| `kai kill <pid\|name>` | stops a process |
| `kai import [file\|folder]` | imports a `kai.json`/`kai.yml`/`kai.yaml` as a project; with no argument, looks in the current folder |
| `kai raise [--level info\|warning\|error] [--title T] <msg>` | tray notification from scripts (also kept in the notification history) |
| `kai show` | brings the Kai window to front |

With no running instance these return a connection error (exit 2). Opening
Kai a second time just brings the window back (**single instance**).

## Standalone

- `kai validate <file>` — checks a `kai.json`/`kai.yml` (structure,
  duplicate or reserved CLI paths); exit 1 on errors.
- `kai help` — summary of everything above.

Reserved names that can never be a `cli_path`: `run`, `list`, `env`,
`show`, `help`, `import`, `validate`, `ps`, `attach`, `kill`, `raise`,
`history`, `last`, `init`, `completion`.
The bare word `global` is **not** a flag anymore — use `-g`/`--global`.
Flags always come before the path.

## Autocomplete (Tab)

`kai completion bash|zsh|powershell` prints a completion script; the shell
calls the hidden `kai __complete <words...> <partial>` on each Tab, which
answers one candidate per line (same local/global resolution as a real run:
paths, top-level verbs, `--option=`, fixed `select`/`bool` values).

```bash
eval "$(kai completion bash)"   # ~/.bashrc — also works with a `kai` function wrapping kai.exe
```

Folders can carry a `cli_description`, shown next to their segment when the
CLI lists them.

## Windows / WSL

`kai.exe` is a GUI-subsystem app that attaches to the parent console, so
the CLI works from cmd, PowerShell and from WSL through interop (e.g.
`alias kai='kai.exe'`). In local mode from a WSL folder (`\\wsl.localhost\<distro>\...`)
the command runs inside that same distro through `wsl.exe`, with
`{{PROJECT_PATH}}` converted to its Linux path — same result as a native
Kai. Global mode applies the app's terminal targets (default included),
persisted dynamic variables and graceful-stop timeout, like the UI.

## `kai kip` — helper for KIP programs

`kai kip <verb> …` prints one KIP protocol message per call and reads
values out of Kai's answers (`kai kip get "$resp" values.env`). It is pure
and offline: no app, no config, no IPC. See [kip.md](kip.md) §6; `kai kip
--help` lists the verbs. `kip` is a reserved verb, so a command path can't
be called `kip`.

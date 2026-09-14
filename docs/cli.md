# CLI — Kai in the terminal

The same `kai` binary works three ways:

| Mode | When | What it runs |
|------|------|--------------|
| **Local CLI Paths** | current directory has `kai.yml` | commands from **that file**, no running Kai needed |
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

### Working directory

By default a command runs in its own working directory (`working_dir`,
inherited from its folder), also when started from the CLI — a global run
happens inside the app, so the terminal's directory would otherwise be lost.
A command can opt in to run **where you typed `kai`** instead:
`cli_working_dir: invocation` in the file, or the *Run in the directory I
invoked it from* checkbox in the command editor's **CLI Interface** tab. It
applies to local and global (`-g`) runs, shows in `--dry-run`, and never
affects runs started from the Kai window.

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
| `kai import [file\|folder] [--only a,b]` | imports a `kai.yml` as a project; with no argument, looks in the current folder. A file with the `kai_export` header (an exported package) is imported **whole and without questions** — commands, folder and global actions, collections, profiles, environments — or only the parts named in `--only` (`commands`, `collections`, `settings`, `environments`, `terminal-profiles`, `actions`; `actions` = global actions and the commands the actions use, folder actions come with `commands`) |
| `kai raise [--level info\|warning\|error] [--title T] <msg>` | tray notification from scripts (also kept in the notification history) |
| `kai show` | brings the Kai window to front |

With no running instance these return a connection error (exit 2). Opening
Kai a second time just brings the window back (**single instance**).

## Standalone

- `kai validate <file>` — checks a `kai.yml` (structure,
  duplicate or reserved CLI paths); exit 1 on errors.
- `kai help` — summary of everything above.

Reserved names that can never be a `cli_path`: `run`, `list`, `env`,
`show`, `help`, `import`, `validate`, `ps`, `attach`, `kill`, `raise`,
`history`, `last`, `init`, `completion`.
The bare word `global` is **not** a flag anymore — use `-g`/`--global`.
Flags always come before the path.

## Autocomplete (Tab)

The first time Kai runs in an interactive terminal it **asks once** whether to
turn on Tab autocomplete for that terminal, listing exactly the files it would
write. The answer is stored per environment (bash, zsh, PowerShell, each WSL
distro...) — decline and it never asks again; accept and the files are kept
up to date by later Kai versions. Nothing is written without a yes, and
nothing is asked when input or output is redirected (scripts, CI, pipes).

| Command | Action |
|---------|--------|
| `kai completion install [bash\|zsh\|powershell\|pwsh]` | turns it on without being asked (no shell given: detects the one you're in) |
| `kai completion uninstall [shell]` | removes what Kai wrote and remembers the "no" |
| `kai completion bash\|zsh\|powershell` | just prints the script, for manual setups |

What gets written (all of it also removable by hand):

- **bash** — two small files under `~/.local/share/bash-completion/completions/`
  (`kai`, `kai.exe`), which `bash-completion` loads on its own: no `.bashrc`
  edit, no `source`. Only when `bash-completion` isn't installed does Kai add a
  marked block to an existing `~/.bashrc`.
- **zsh** — `~/.local/share/kai/completion.zsh` plus a marked block in `~/.zshrc`.
- **PowerShell** (Windows PowerShell and PowerShell 7) — a marked block in your
  `$PROFILE` (the real *Documents* folder, OneDrive-redirected or not). Needs a
  profile-friendly execution policy (the default `RemoteSigned` is fine).

If Kai has **no permission** on one of those files (or on the folder holding it), it says which file and stops: it never pretends it installed or removed something it could not touch. Fix the owner/permissions and run `kai completion install` again.

Marked blocks are delimited by `# >>> kai completion >>>` / `# <<< kai
completion <<<`. The shell calls the hidden `kai __complete --cur=<partial>
<words...>` on each Tab, which answers one candidate per line (same
local/global resolution as a real run: paths, top-level verbs, `--option=`,
fixed `select`/`bool` values). The script finds `kai` or `kai.exe` by itself,
so a `kai() { kai.exe $@; }` wrapper or `alias kai=kai.exe` is not needed for it.

From **WSL** with the Windows `kai.exe`: run `kai.exe` once from a Linux folder
and answer the question — Kai writes into *that* distro (read from the current
folder), and the decision is stored on the Windows side as `wsl:<distro>:<shell>`.

Folders can carry a `cli_description`, shown next to their segment when the
CLI lists them.

## Windows / WSL

`kai.exe` is a GUI-subsystem app (Kai lives in the tray and can't open a console when started), so on its own it has no console: it uses the handles it inherits or attaches to the parent console. Terminals that host the shell in a pseudo-console (JetBrains IDEs such as PhpStorm) can leave its output blank, and `cmd`/PowerShell don't wait for a GUI program to finish. For that the installer also puts a small **console** program, **`kai.com`**, next to it: typing `kai` in a terminal finds it first (`.com` comes before `.exe` in `PATHEXT`), and it runs `kai.exe` with the same arguments, waits, and returns the same exit code. Redirects (`kai list | more`, `> file`) work as usual, and `Ctrl+C` reaches the command. Shortcuts, the tray and startup keep using `kai.exe`; typing `kai.exe` explicitly skips the shim. The CLI works from cmd, PowerShell and from WSL through interop (e.g.
`alias kai='kai.exe'`; see Autocomplete above for Tab). In local mode from a WSL folder (`\\wsl.localhost\<distro>\...`)
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

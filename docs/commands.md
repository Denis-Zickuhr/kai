# Commands

A **Command** runs a program on your machine. The text you write is either a
line (or script) for your shell — the default, **Native** — or **code** in
Python, Node or PHP.

## How to create one

Menu **Item → New command**, **Command** tab. Fill in the command and,
optionally, the **working directory**. The **Language** selector (top right of
the command field) switches between Native, Python, Node and PHP.

```bash
npm run dev
```

> In `kai.yml` the type is `"command"`. The old `"shell"` is still accepted
> when reading files.

## Languages

| Language | The text is | Runs as |
|---|---|---|
| **Native** (default) | a shell line or script | your shell (`bash -c`, or the terminal target's shell) |
| **Python** | Python code | `python3 -u -c <code>` |
| **Node** | JavaScript code | `node -e <code>` |

```yaml
- name: "Sizes"
  type: "command"
  language: "python"
  command: "import os\nfor f in os.listdir('.'):\n    print(f, os.path.getsize(f))"
```

No `python3 -c "..."` and no heredoc: you write the code, Kai builds the call.
It sends the code to the interpreter compressed and base64-encoded and the
interpreter decodes it itself, so

- there is **no quoting problem** — any quote, `$`, backtick or newline works;
- **stdin stays free** (KIP reads Kai's answers from it, a script can `input()`);
- the call is just one more shell line, so it goes through **terminal targets**
  (WSL, Docker, ...) like any other command, with no dependency on `base64`
  or `$(...)` on the other side;
- Python output is unbuffered and UTF-8 (`PYTHONUNBUFFERED=1`,
  `PYTHONIOENCODING=utf-8`, unless you set them yourself).

**Node code runs inside an async function**: top-level `await` works and
`return` ends the script. An uncaught error exits with code 1.

### Variables and parameters

`{{VAR}}` collides with real code (`f"{{x}}"`, dictionaries, JS template
strings), so **Python, Node and PHP code is never interpolated**. Variables and
parameters arrive as **environment variables**, with the usual precedence
(`Global < Folder/Project < Dynamic < Parameters`):

```python
import os
print("Deploying to", os.environ["TARGET"])
```

```js
console.log("Deploying to", process.env.TARGET);
```

```php
echo "Deploying to " . getenv("TARGET");
```

(Native commands keep using `{{VAR}}`.) The editor also stops suggesting
`{{VAR}}` names in a Python/Node/PHP command.

### Which interpreter

- **Settings → Languages** holds the global interpreters: `python3`, `node` and `php`
  by default (on Windows without a terminal target, Python defaults to
  `python`).
- A command can override it: **Interpreter** field (`interpreter` in
  `kai.yml`). Empty = the global one.
- The value is a **shell line resolved where the command runs**. On a WSL
  target it is the WSL's `python3`, and a login shell already loads pyenv and
  nvm — most of the time you don't need to configure anything. It can have
  arguments (`uv run python`, `pipx run python`); quote paths with spaces
  yourself (`"/home/me/my venv/bin/python"`).

### The `kai` module: talk to the Kai app

Every Python/Node command gets a `kai` module injected (nothing to install), with the things the
`kai` CLI already does — notifications, environments, processes, running other commands:

```python
import kai

kai.notify("Backup finished", title="Backup", level="info")   # tray notification + history
print(kai.commands())            # names of the commands registered in Kai
print(kai.env.list(), kai.env.active())
kai.env.use("Prod")              # switch the active environment
kai.run("Deploy")                # start another Kai command in the app (fire and forget)
for p in kai.ps():               # processes Kai tracks: {id, name, pid, status}
    print(p["name"], p["pid"])
kai.kill("api")                  # stop one by name or pid
kai.show()                       # bring the window to the front
kai.import_project("/path/to/kai.yml")
```

```js
await kai.notify('Backup finished', { title: 'Backup', level: 'info' });
console.log(await kai.commands(), await kai.env.active());
await kai.env.use('Prod');
await kai.run('Deploy');
```

In Node `kai` is a global (and `require('kai')`) and every call returns a promise. Failures raise
`kai.Error` (Python) / reject with `kai.Error` (Node): the app refused the request (unknown
environment...) or could not be reached.

How it reaches the app: Kai exports `KAI_IPC_SOCKET` (the local socket the `kai` CLI uses) in the
command's environment. When that socket is not reachable from where the script runs — typically a
script inside **WSL** under a Windows Kai — the module falls back to the `kai` CLI (`KAI_EXE`, or
`kai` / `kai.exe` on `PATH`) for `notify`, `show`, `run`, `env.use` and `kill`; the read operations
(`commands`, `env.list`, `ps`...) need the socket. A script can do nothing here that the `kai` CLI
couldn't already do.

A long example that uses `kip` and `kai` together lives in
[`sample/languages/showcase.py`](../sample/languages/showcase.py).

### Limits

Python/Node/PHP commands can't use **Export variables** (`capture_env`): there is
no `export` to read back — use KIP's `set_env` instead. Everything else
(background, interactive terminal, hooks, conditions, terminal targets, cron,
auto-run, KIP) works as for any command.

### KIP

On a Python or Node command with **KIP** turned on, Kai injects a small `kip` module so a KIP
program is a handful of lines ([kip.md](kip.md#writing-a-kip-program-in-python-or-node)). A **native** command in a POSIX shell
gets a `kip` function instead ([kip.md](kip.md#62-writing-a-kip-program-in-the-shell)).

## Variables (Native)

Use `{{VAR}}` to interpolate variables from the active environment:

```bash
echo "Starting on port {{PORT}} ({{NODE_ENV}})"
```

Precedence: `Global < Folder/Project < Dynamic < Parameters`. See
[variables.md](variables.md).

## Background

Check **Run in background** for long-lived processes (e.g. a dev server).
Kai keeps the process alive, shows its status, and lets you stop it
(SIGTERM → 2s timeout → SIGKILL, applied to the whole process group).

## Interactive

Commands using `read`/prompts accept input right from the output panel's
input field. For full-screen terminal apps (vim, htop, less, a nested
Claude Code), turn on **Interactive terminal** in the command's advanced
settings — it renders a real terminal grid instead of a plain-text ANSI
parser.

## Always succeed, regardless of exit code

Some tools return a non-zero exit code even when they worked fine (a
classic case: `explorer.exe` called from WSL to open a folder in Windows).
Turn on **Ignore exit code** in the command's advanced settings to always
treat it as a success — a real process crash (signal/segfault) is still
reported normally.

## Running in another terminal (e.g. WSL)

See [terminal-targets-wsl.md](terminal-targets-wsl.md).

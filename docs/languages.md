# Languages: Python, Node & PHP

A command's text can be **code** instead of a shell line. Pick the **Language** at the top right of the command field: **Native (shell)** (the default), **Python**, **Node** or **PHP**. No `python3 -c "..."`, no heredoc: you write the code, Kai builds the call.

```
import os
for f in os.listdir('.'):
    print(f, os.path.getsize(f))
```

## How it runs

Kai hands the code to the interpreter encoded, so there are **no quoting problems** (any quote, `$`, backtick or newline works) and **stdin stays free**: `input()` works and so does [KIP](kip.md). The call is just one more shell line, so it goes through [execution profiles](terminal-targets-wsl.md) such as WSL.

- Python output is unbuffered and UTF-8 — there is no `flush` to forget.
- Node code runs inside an async function: top-level `await` works and `return` ends the script. An uncaught error exits with code 1.
- PHP runs through `php -r`; a leading `<?php` is accepted and ignored. A `namespace` or `declare(strict_types=1)` at the top of your code works.
- **Exit code** decides success as usual (`sys.exit(3)`, `process.exitCode = 3`, `exit(3)`).

## Variables and parameters

`{{VAR}}` collides with real code (`f"{{x}}"`, dictionaries, JS template strings), so the code of these languages is **never interpolated** (Python, Node, PHP). Variables and [parameters](parameters.md) arrive as **environment variables**, with the usual precedence:

```
import os
print("Deploying to", os.environ["TARGET"])
```

```
console.log("Deploying to", process.env.TARGET);
```

```
echo "Deploying to " . getenv("TARGET");
```

## Which interpreter

- **Settings → Languages** holds the global ones: `python3`, `node` and `php` by default (on Windows without an execution profile, Python defaults to `python`).
- A command can override it in its **Interpreter** field.
- The value is a **shell line resolved where the command runs**: on a WSL profile it is the WSL's `python3`, and a login shell already loads pyenv and nvm. It can have arguments (`uv run python`); quote paths with spaces yourself.

## The `kai` module: talk to the app

Every Python/Node command has a `kai` module injected (nothing to install) — the same things the [kai CLI](cli.md) does:

```
import kai
kai.notify("Backup finished", title="Backup", level="info")   # tray notification + history
kai.commands()              # names of the registered commands
kai.env.list(); kai.env.active(); kai.env.use("Prod")
kai.run("Deploy")           # start another command (fire and forget)
kai.ps(); kai.kill("api")   # tracked processes: {id, name, pid, status}
kai.show()                  # bring the window to the front
kai.import_project("/path/to/kai.yml")
```

```
await kai.notify('Backup finished', { title: 'Backup', level: 'info' });
console.log(await kai.commands(), await kai.env.active());
await kai.env.use('Prod');
await kai.run('Deploy');
```

In Node `kai` is a global (and `require('kai')`) and every call returns a promise. Failures raise `kai.Error`. Kai exports `KAI_IPC_SOCKET`; when the socket can't be reached (a script in WSL under a Windows Kai) the module falls back to the `kai` CLI for notify, show, run, env.use and kill.

## The `kip` module: turn the command into an app

With **KIP** turned on, the command text is the whole program and a `kip` module is injected too (`import kip`; in Node the global `kip`; in PHP the class `Kip`):

```
import kip
v = kip.prompt([{"name": "env", "type": "select", "options": ["dev", "prod"]}],
               id="where", title="Where to?")
kip.progress(50, "Deploying...")
kip.done(title="Deployed to " + v["env"])
```

```
$v = Kip::prompt([['name' => 'env', 'type' => 'select', 'options' => ['dev', 'prod']]],
                 ['id' => 'where', 'title' => 'Where to?']);
Kip::progress(50, 'Deploying...');
Kip::done('Deployed to ' . $v['env']);
```

Back raises `kip.Back` (`KipBack` in PHP), Cancel exits with code 130, `hello` is sent for you. Full reference in [KIP](kip.md). Without KIP the module still exists and tells you what to turn on when you use it. PHP has no `kai` module.

## Scripts you run as files

The modules above are injected into the code of a Python/Node/PHP *command*. A script that runs as a **file** — your own `tool.py`, called from a Native command with **KIP** on (`python3 tool.py`, `node tool.js`, `php tool.php`) — has no such bootstrap, so Kai gives it the same modules from disk. It keeps them in a folder of its own in the temp folder (`kai-run/modules-…`) and exports where they are, **in front of** whatever you already have in those variables:

| Language | Variable | In your script |
|---|---|---|
| Python | `PYTHONPATH` | `import kip`, `import kai` |
| Node | `NODE_PATH` | `require('kip')`, `require('kai')` — CommonJS only; `import` in ES modules ignores `NODE_PATH` |
| PHP | `PHP_INI_SCAN_DIR` | the class `Kip` is already there — no `require`. `php -r` does not read it: use `require getenv('KAI_MODULES') . '/kip.php';` |
| any | `KAI_MODULES` | the folder itself |

- Only a **Native command with KIP on** gets them, so a `kai` or `kip` package of your own project is never shadowed by a shell command that did not ask for KIP. Python/Node/PHP commands always have them.
- They do not cross an execution profile (WSL, Docker, SSH): there the variables are not passed. The code of a Python/Node/PHP command still gets the modules injected as always.
- With KIP off, `kip` is a stub that explains what to turn on.

## The shell is Native

There are no bash, sh or PowerShell languages: **Native** already is a shell script (bash, and sh where there is no bash), with `{{VAR}}` and `{% if %}` replaced as always. With **KIP** on, in a POSIX shell (Linux, macOS, WSL and other POSIX profiles), Kai defines a `kip` function before the text runs — the same verbs as `kai kip`, with no `kai` binary:

```
kip prompt --id p --field text name Name --required
kip recv                          # the answer lands in $KIP_MSG
kip done --title "Hello, $(kip get "$KIP_MSG" values.name)!"
```

It needs `awk` (and `base64` + `gzip`) where the command runs. See [KIP §6.2](kip.md). Old files with `language: bash|sh|pwsh` still load, as Native.

## Very long commands

A command that does not fit the system's command line (Windows `cmd.exe` cuts at 8191 characters; Linux at ~128 KB) needs nothing from you: Kai writes the text to a file of its own in the temp folder (`kai-run`, cleaned after a few days) and runs it from there — `. 'file'` for a Native command, `python3 -u file.py` / `node file.cjs` / `php file.php` for Python, Node and PHP. It applies locally (on Windows too, for Python/Node/PHP) and to WSL. On a **WSL profile** the variables of the environment (Global, Folder, Dynamic, Parameters — a big JWT is enough) travel inside the command text, so they count too: when the line plus the variables does not fit, the file also carries the `export`s and the line is just `. 'file'`, whatever the size of the token. The file is readable by you only. On Docker/SSH profiles, where that file would not exist, the command goes as it is (with a warning in the output when it is too long).

## Limits

**Export variables** does not apply to Python, Node and PHP (there is no `export`): use KIP's `set_env`. Everything else — background, hooks, conditions, profiles, schedules — works as for any command.

> **Tip:** A long example using both modules ships with Kai: `sample/languages/showcase.py`.

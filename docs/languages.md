# Python & Node

A command's text can be **code** instead of a shell line. Pick the **Language** at the top right of the command field: **Native (shell)** (the default), **Python** or **Node**. No `python3 -c "..."`, no heredoc: you write the code, Kai builds the call.

```
import os
for f in os.listdir('.'):
    print(f, os.path.getsize(f))
```

## How it runs

Kai hands the code to the interpreter encoded, so there are **no quoting problems** (any quote, `$`, backtick or newline works) and **stdin stays free**: `input()` works and so does [KIP](kip.md). The call is just one more shell line, so it goes through [execution profiles](terminal-targets-wsl.md) such as WSL.

- Python output is unbuffered and UTF-8 — there is no `flush` to forget.
- Node code runs inside an async function: top-level `await` works and `return` ends the script. An uncaught error exits with code 1.
- **Exit code** decides success as usual (`sys.exit(3)`, `process.exitCode = 3`).

## Variables and parameters

`{{VAR}}` collides with real code (`f"{{x}}"`, dictionaries, JS template strings), so Python/Node code is **never interpolated**. Variables and [parameters](parameters.md) arrive as **environment variables**, with the usual precedence:

```
import os
print("Deploying to", os.environ["TARGET"])
```

```
console.log("Deploying to", process.env.TARGET);
```

## Which interpreter

- **Settings → Languages** holds the global ones: `python3` and `node` by default (on Windows without an execution profile, Python defaults to `python`).
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
kai.import_project("/path/to/kai.json")
```

```
await kai.notify('Backup finished', { title: 'Backup', level: 'info' });
console.log(await kai.commands(), await kai.env.active());
await kai.env.use('Prod');
await kai.run('Deploy');
```

In Node `kai` is a global (and `require('kai')`) and every call returns a promise. Failures raise `kai.Error`. Kai exports `KAI_IPC_SOCKET`; when the socket can't be reached (a script in WSL under a Windows Kai) the module falls back to the `kai` CLI for notify, show, run, env.use and kill.

## The `kip` module: turn the command into an app

With **KIP** turned on, the command text is the whole program and a `kip` module is injected too (`import kip`; in Node the global `kip`):

```
import kip
v = kip.prompt([{"name": "env", "type": "select", "options": ["dev", "prod"]}],
               id="where", title="Where to?")
kip.progress(50, "Deploying...")
kip.done(title="Deployed to " + v["env"])
```

Back raises `kip.Back`, Cancel exits with code 130, `hello` is sent for you. Full reference in [KIP](kip.md).

## Limits

**Export variables** does not apply (there is no `export`): use KIP's `set_env`. Everything else — background, hooks, conditions, profiles, schedules — works as for any command.

> **Tip:** A long example using both modules ships with Kai: `sample/languages/showcase.py`.

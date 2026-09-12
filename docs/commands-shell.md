# Shell commands

A **Shell** command runs a line (or script) in your shell.

## How to create one

Menu **Item → New command**, **Shell** tab. Fill in the command and,
optionally, the **working directory**.

```bash
npm run dev
```

## Variables

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

# Terminal targets & WSL

A Shell command can run through a **terminal target** — a template that
wraps the final command. Classic case: running in **WSL** from Kai running
natively on Windows.

## Configuring one

**Settings → Terminal Targets**. Kai ships a default **WSL** target on
Windows (used automatically when no other target is set as default):

```
Name:     WSL
Template: wsl.exe -- bash -lic 'eval "$(echo "$1" | base64 -d)"' kai {{command_b64}}
```

## Placeholders

- `{{command}}` — the interpolated command, with **safe quote escaping**.
- `{{command_b64}}` — **crash-proof** injection (base64). Useful when the
  command has quotes, line breaks or special characters — the default WSL
  target above uses it, decoding and `eval`-ing the command inside the
  same bash process (whose stdin is the ConPTY's tty, so `read`,
  `docker -it`, interactive scripts and pipes all just work).

## Why `-lic`, not just `-lc`

`-l` (login shell) alone does **not** read `~/.bashrc` — it reads
`~/.profile`/`~/.bash_profile` instead, and a stock Ubuntu `.bashrc` exits
immediately for non-interactive shells (a `case $- in *i*) ;; *) return;;
esac` guard at the top). Adding `-i` (interactive) makes `$-` contain `i`,
so the guard passes and your aliases/functions actually load. If you write
your own terminal target and want it to see your shell customizations,
include `-i` too.

## `working_dir` via WSL

A command's `working_dir` is applied with a `cd` **inside** the shell —
the Windows process's (`wsl.exe`'s) CWD never crosses into the WSL side,
and a login shell (`bash -l`) resets to `$HOME` anyway.

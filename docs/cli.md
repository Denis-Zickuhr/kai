# CLI — control Kai from the terminal

With Kai **already running**, use the same binary as a client (CopyQ
vibe). Communication happens over a `QLocalServer`/local socket; opening
Kai a second time just brings the window back (**single instance**).

| Command | Action |
|---------|------|
| `kai run "Deploy"` | runs a command by **name** |
| `kai list` | lists available commands |
| `kai env list` | lists environments |
| `kai env use Prod` | activates an environment |
| `kai ps` | lists running processes |
| `kai attach <pid\|name>` | attaches to a process's output |
| `kai kill <pid\|name>` | stops a process |
| `kai show` | brings the Kai window to front |
| `kai help` | CLI help |

## Use in scripts / CI

```bash
kai env use staging
kai run "Run migrations" && kai run "Seed"
```

## Notes

- With no running instance, the CLI returns a connection error (exit 2).
- On **Windows**, `kai.exe` is a GUI app (no console): CLI stdout doesn't
  show up in a native console unless launched from one that Kai can attach
  to — the CLI is mainly aimed at Linux/scripts.

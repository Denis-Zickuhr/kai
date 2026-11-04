# Kai Stop Test

Reproduces "a script runs a Go binary that connects to a terminal and runs a script there"
to check that **Stop** really stops everything, like Ctrl+C in a terminal.

- `termbridge/` — small Go program (stand-in for the real binary). Opens a pty, starts
  `remote.sh` on it in its own session, forwards SIGINT as `^C`, exits 130 when the
  remote finishes. It does not handle SIGTERM.
- `remote.sh` — the "PHP script": heartbeat every second; on `^C` it cleans up for
  `CLEANUP_SECS` (default 4) and exits. It ignores SIGHUP, so it survives losing the bridge.
- `run.sh` — wrapper that runs the bridge and prints its exit code. Builds it on first run
  (`./build.sh`: local Go, or Docker `golang:1.22-alpine`).
- `check.sh` — shows what is still alive after Stop (status file, heartbeat, processes, containers).
- `compose.yml` — two containers whose PID 1 ignores SIGTERM (`docker compose up`).

In Kai: open this folder, run command 1, press Stop, then run command 4.
In a terminal: `./run.sh`, Ctrl+C → exit 130 and `status: clean`.

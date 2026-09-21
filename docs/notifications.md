# Notifications & logs

## What Kai notifies

Notifications are native desktop notifications (system tray). If your system has no tray icon, they simply won't appear. Under **Settings → Notifications** there is a master switch and one per event:

- A command **failed** to run — covers pre-hooks, the main command (error, crash or HTTP ≥ 400) and post-hooks, including runs started by [auto-run](scheduling.md).
- A background process **crashed** or exited with an error.
- A background process **finished successfully** (off by default).
- A corrupted configuration file was **restored from a backup**.
- The **first ERROR line** in a [formatted output](output.md) — once per run (off by default).
- **Notify even while the Kai window is focused** — by default Kai stays quiet when you are already looking at it, since the tree already shows the red badge.

A command stopped on purpose (Stop, kill, reset) is not a failure and never notifies.

## Send your own

- From a script in the terminal: `kai raise --level warning --title Backup "disk almost full"`.
- From Python or Node: `kai.notify("done", title="Backup")` — see [Python & Node](languages.md).
- From a KIP program: the `notify` message.
- For a scheduled command: **Notify when executed** (see [Schedules](scheduling.md)).

## The history

The status line at the bottom shows how many notifications are **unread**; click it to open the history and read them. They are kept between sessions.

## Application logs

**File → Logs** opens Kai's own log (what Kai itself did — not your commands' output). Filter by minimum level (Debug, Info, Warning, Error), see the path of the log file, or clear the view. It is the first place to look when something misbehaves.

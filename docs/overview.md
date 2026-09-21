# Kai overview

**Kai** is a native command runner for developers: it keeps the commands you run every day — shell, Python, Node, HTTP requests — one global shortcut away, injects variables per environment, streams the output live and never leaves orphaned processes behind.

## Typical workflow

1. Create **folders** (they become tabs) and, inside them, **commands** — see [Folders, tree & search](organizing.md).
2. Write the command: a [shell line](commands.md), [Python or Node code](languages.md) or an [HTTP request](commands-http.md). Ask for values with [parameters](parameters.md).
3. Set **variables** in an [environment](environments.md) (the selector at the top) and use them as `{{VAR}}`.
4. Run with a double-click or **Enter**; follow the [Output](output.md) live.
5. Automate: [hooks](hooks.md), [schedules](scheduling.md), [auto-replies](responders.md), and the [command line](cli.md).

## The main window

- **Top bar:** menus **File** (import, export, run history, logs, hide, quit), **Item** (new folder / command / collection), **Settings** and **Help**; the environment selector sits on the right.
- **Tabs and tree:** one tab per folder, with the commands and subfolders in a tree; the side bar next to it runs, stops, edits and creates things.
- **Output panel:** the result of the selected command (bottom by default; it can sit left or right in [Settings → Layout](settings.md)).
- **Status line:** how many commands are running (click for the [process list](processes.md)), whether the last run succeeded and the unread [notifications](notifications.md).

> **Tip:** Press **Ctrl+F** to search commands by name. The [global shortcut](shortcuts.md) shows or hides Kai from anywhere.

## Where to read next

|   |   |
|---|---|
| [Commands](commands.md) | what a command can do and its options |
| [Python & Node](languages.md) | code instead of a shell line, plus the built-in `kai` and `kip` modules |
| [KIP](kip.md) | turn a command into a small app with forms, progress and results |
| [kai.json](kai-json.md) | keep a project's commands in a versionable file |
| [Settings](settings.md) | appearance, layout, notifications, interpreters and more |

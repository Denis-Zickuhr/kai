# Settings

Menu **Settings**. Changes apply when you press **OK** (the **Storage** page is the exception: its actions are immediate).

|   |   |
|---|---|
| **General** | interface **language** (English/Portuguese; restart to translate the whole main window), **start Kai with the system**, the **graceful stop timeout** (how long Stop waits after asking a process to exit before forcing it), **max log size** per command. |
| **Appearance** | the active [theme](themes.md) and *Import theme…*; window behaviour — hide when it loses focus, start visible, open as fixed size / remember last size / maximized / fullscreen; visual effects (shadows, translucent window, liquid glass blur, animations, theme gradients — all off by default, enable them to match your environment). |
| **Layout** | **density** (comfortable/compact), **corners** (straight, soft, rounded — every window and dialog follows it), tree connector lines, a background image for the command list and its opacity, where each group of action buttons goes (top, bottom, left, right or hidden), and the **Output position**. |
| **Shortcuts** | the global show/hide shortcut and a searchable table of every action — see [Keyboard shortcuts](shortcuts.md). *Restore Defaults* resets it. |
| **Execution Profiles** | the targets a command can run through (WSL, containers…) — see [Terminals & WSL](terminal-targets-wsl.md). |
| **Languages** | the Python, Node and PHP interpreters — see [Languages](languages.md). |
| **Storage** | every command, folder and collection in one filterable table, with bulk **duplicate** and **delete**. Deleting a folder moves its contents up one level instead of deleting them. Applies immediately. |
| **Notifications** | the master switch and one switch per event — see [Notifications & logs](notifications.md). |
| **KIP** | wait times (first message, field updates, after Cancel), remembering the last answers (and a button to forget them all), opening the Details drawer on failure, and the size of the KIP own window (follow the general setting, normal, maximized or fullscreen) — see [KIP](kip.md). |

## Where things are stored

Settings, commands, collections, history and notifications are JSON files in your user config folder (`~/.config/kai/` on Linux, `%APPDATA%\Kai\` on Windows). A corrupted file is restored from an automatic backup and Kai tells you. Themes live in `themes/` there.

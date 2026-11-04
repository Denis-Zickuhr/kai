# Settings

Menu **Settings**. Changes apply when you press **OK** (the **Storage** page is the exception: its actions are immediate). Scrolling over a list, switch or number field only scrolls the page: the mouse wheel never changes a value.

|   |   |
|---|---|
| **General** | interface **language** (English/Portuguese; restart to translate the whole main window), **start Kai with the system**, the **graceful stop timeout** (how long Stop waits after asking a process to exit before forcing it), **max log size** per command, the Python, Node and PHP interpreters (see [Languages](languages.md)), **smart focus** (off by default). On: selecting a command brings its Output forward and selecting a folder brings its Document. Off: browsing the tree leaves both alone. The Output comes up when you run a command or click its Output tab; **Show output** and **Open document** (next to Run, and in the right-click menu) bring the selected item's output or Document; a monitor mark in the tree flags commands with output (highlighted: the one in the Output). **Inline actions** (off by default): the item's actions show at the right edge of its tree row (on the hovered and the selected row, Run or Stop always in the corner) — Run/Stop, Reset, Quick edit (HTTP), Show output and Open document, following the item's state. The Execution button group is hidden while it is on (it would repeat them); the shortcuts and the context menu stay. |
| **Appearance** | the active [theme](themes.md) and *Import theme…*; window behaviour — hide when it loses focus, start visible, open as fixed size / remember last size / maximized / fullscreen; visual effects (shadows, translucent window, liquid glass blur, animations, theme gradients — all off by default, enable them to match your environment). |
| **Layout** | **density** (comfortable/compact), **corners** (straight, soft, rounded — every window and dialog follows it), tree connector lines, a background image for the command list and its opacity, where each group of action buttons goes (top, bottom, left, right or hidden), and a **Restore layout** button (the panels themselves are moved from the [View menu](layout.md)). |
| **Shortcuts** | the global show/hide shortcut and a searchable table of every action — see [Keyboard shortcuts](shortcuts.md). *Restore Defaults* resets it. |
| **Execution Profiles** | the targets a command can run through (WSL, containers…) — see [Terminals & WSL](terminal-targets-wsl.md). |
| **Storage** | every command, HTTP request, folder, collection and note in one filterable table (filter chips by type, with counts), with bulk **duplicate** and **delete**. Deleting a folder moves its contents up one level instead of deleting them. Applies immediately. |
| **Notifications** | the master switch and one switch per event — see [Notifications & logs](notifications.md). |
| **AI** | the AI providers (key, address, model — kept in `ai.json`, never exported), the default provider, the agent step limit, extra instructions, the chat history (*Delete all*) and the **MCP bridge** for external agents — see [AI assistant](ai.md). |
| **KIP** | wait times (first message, field updates, after Cancel), remembering the last answers (and a button to forget them all), opening the Details drawer on failure, and the size of the KIP own window (follow the general setting, normal, maximized or fullscreen) — see [KIP](kip.md). |

From the tray menu, **Settings**, **Import** and **Quick run** open by themselves — the main window stays hidden. **Logs** and **Notifications** are panels of the window, so they bring it up.

## Where things are stored

Settings, commands, collections, history and notifications are JSON files in your user config folder (`~/.config/kai/` on Linux, `%APPDATA%\Kai\` on Windows). AI providers (with their keys) are in `ai.json` (mode `0600`) and the chats in `ai-conversations/` — neither is ever exported or synced. A corrupted file is restored from an automatic backup and Kai tells you. Themes live in `themes/` there.

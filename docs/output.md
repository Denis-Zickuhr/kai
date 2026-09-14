# The Output panel

The panel shows what the selected command printed, live. It sits at the bottom by default; **Settings → Layout** can move it left or right, and **Settings → General** can collapse it when you select a folder.

## The output bar

Above the panel there is a **bar with one tab per command that ran in this session** — the ones still running and the ones that already finished — so you can jump between outputs without hunting for the command in the tree. A folder action has its own tab per folder ("Command · Folder"). The bar is **always there** (empty, it shows a hint) and only hides while the panel is collapsed.

- **Each tab** shows a status dot (running, waiting for your answer in a KIP command, finished, failed, skipped), the name, the elapsed time while it runs, and a small dot when new output arrived in a tab you are not looking at. Hover for the details.
- **Click a tab** to bring that output up: the command is selected in the tree too (switching to the right root folder if it lives in another project).
- **Close a tab** with the × (it shows on the tab in focus and on hover) or with a middle click. It only closes the **tab**: the command keeps running and its log is kept. Click the command in the tree (or run it again) and the tab comes back. Closing the tab in focus moves to its neighbour; closing the last one clears the panel.
- **Right click** opens the tab menu — *Stop*, *Run again*, *Show in the command tree*, *Close output*, *Close the other outputs*, *Close finished outputs* — followed by the usual output menu (clear, copy all, export, display options). Right click **does not bring the tab to the front**: the menu acts on the tab you clicked (clear, copy and export use *its* log) while you keep looking at your current output; only *Show in the command tree* switches to it.
- **Many tabs:** the mouse wheel scrolls the bar and a button at its end lists them all.
- **Keyboard:** **Ctrl+Tab** and **Ctrl+Shift+Tab** switch to the next / previous output (rebindable in **Settings → Shortcuts**).

Tabs are not saved: they start empty on the next launch, like the processes themselves.

## Tabs

- **Output** — the command's stdout/stderr, with ANSI colors.
- For HTTP commands also **Response** (a navigable JSON tree), **Headers** and **Request** (exactly what was sent) — see [HTTP Commands](commands-http.md).
- [KIP](kip.md) commands replace the tabs with their own screen.

The header shows the command and its state: *idle*, *running*, *done* or *error*.

## Answering a running command

The field at the bottom sends text to the command's input — for `read` prompts, confirmations, a REPL. Click it or press **Ctrl+'** to focus it (and again to go back to the tree). [Auto-replies](responders.md) can do it for you.

## Search and display options

- **Search in output** (magnifier) finds text in the log.
- The **display options** menu has: Auto-scroll, Line numbers, Timestamps, Wrap long lines (visual only), **Compact output** (collapses repeated blank lines and trims trailing spaces), Increase/Decrease font, Copy all, **Export to file…** and Clear output.
- Large outputs are capped per command: **Settings → General → Max log size per command**.

## Other ways to see the output

|   |   |
|---|---|
| **Interactive terminal** | a real terminal grid for `vim`, `htop`, `less`, a nested Claude Code. Turn it on in the command's advanced settings. |
| **Formatted output** | each line that is a JSON log record (level, message, timestamp) becomes a collapsible card with a level badge; other lines stay as text. The search can filter by *level*, and Kai can [notify](notifications.md) on the first ERROR line. |

## Detach into a window

The pop-out button opens the panel in its own window with the same tabs and theme, so you can keep a long log beside your editor. From the terminal, `kai -gw ` runs a command and opens its window (see [CLI](cli.md)).

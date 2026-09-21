# The Output panel

The panel shows what the selected command printed, live. It sits at the bottom by default; **Settings → Layout** can move it left or right, and **Settings → General** can collapse it when you select a folder.

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
| **Render as Markdown** | renders the accumulated output as Markdown (headings, lists, links, code), with scrolling and search. Takes priority over Formatted output. |

## Detach into a window

The pop-out button opens the panel in its own window with the same tabs and theme, so you can keep a long log beside your editor. From the terminal, `kai -gw ` runs a command and opens its window (see [CLI](cli.md)).

# Panels & layout

The Kai window is a board of **panels** that you can move, close and bring back — a bit like VS Code or JetBrains, but light. The default layout has the **command list on the left** and the **Output on the right**, half the window each, both with their tab strip showing (so every panel can be dragged right away). It is what you see until you change something; the old arrangement (list on top, Output below) is the **Classic** perspective.

## The grid

Panels live in the **cells** of a grid of **columns × rows**, from 1 to 5 each (the default is 2 × 2). A cell with more than one panel shows them as tabs (no × on the tab: close from the right click menu). By default: *Commands* in the first cell, *Output* (with the Document as a tab) in the second.

A cell with **no panel disappears** and its neighbours take its place. Two fill modes (**Settings → Layout → Panel grid**, with a small preview):

- **Fill vertically** (default): each column is a stack, top to bottom, of its filled cells only. A column with a single filled cell gives it the whole column; a column with none is gone and the others share the width.
- **Fill horizontally**: the same, turned: each row is a stack, left to right, of its filled cells; a row with a single cell gives it the whole row.
- **Fit**: nothing is absorbed; every cell is one square of the grid and empty squares stay empty. Drag the dotted handle at a cell's bottom-right corner (on the board, or on the preview in Settings) toward the size you want and the cell takes more columns/rows, like pieces in a suitcase inventory. A cell cannot grow over another filled cell; if a panel is dropped on a square it covered, the big cell shrinks back. Spans are saved in `layout.json` (`col_span` / `row_span`, only when above 1).

Drawn on a 3 × 3 grid, with `■` filled and `·` empty — *vertical*: `■ · ·` / `■ ■ ■` / `■ · ·` has the first column in three parts and the middle cell of the others as full-height columns; *horizontal*: `■ · ·` / `· ■ ·` / `· · ■` is three full-width rows.

Making the grid smaller never loses a panel: the panels of the cells that go away become tabs of the nearest remaining cell. Changing the shape or the mode starts the sizes from equal parts.

| Panel | What it is | Opens in |
|---|---|---|
| **Commands** | the tree, its search and action buttons | first cell |
| **Output** | the tabs of what ran, with logs and responses | last column of the first row |
| **Document** | the README / notes reader, so you can read it **while a command runs** — a tab next to the Output | with the Output |
| **Processes** | what is running now, with stop / force stop | with the Output |
| **History** | the run history, with re-run | with the Output |
| **Notifications** | the notification center | with the Output |
| **Variables** | the environments screen: packages of variables and the dynamic variables, edited in place | with the Output |
| **AI chat** | the assistant's chat — **only exists once you add a provider** | with the Output |
| **Run panels** and **pinned KIP commands** | see below | with the Output |

Everything that used to open as a modal window (processes, history, notifications, the AI chat) is now one of these panels: the same screen, but docked wherever you want it.

## Documents and Output

The **Document** is a normal panel, a tab beside the **Output** by default. The tree drives both, and neither wipes the other:

- **Select a folder** with a README (or notes): the Document tab comes to the front. The Output keeps showing the last run.
- **Select a folder with nothing to show** (no working directory, no notes): the Output goes idle, as it always did — a folder never shows a command's output.
- **Select a command**: the Output comes to the front (whether or not it has run yet), and the Document panel quietly switches to the README of the command's folder — only when that folder has something to show, and never while you are editing a document.
- **Click the item that is already selected**: its panel comes to the front again (a folder with documents → Document, a command → Output).
- **Closed Document panel**: selecting a folder does not reopen it (**View → Document** does). Selecting a *note* does open it — a note has no other place to be read.
- Selecting never undoes a maximized panel and never opens a collapsed side strip.

Drag the Document tab to the center, the bottom or a side and you read the README **beside** the run.

## The View menu

**View** in the top bar is the way back to everything:

- **The panel list** — a highlighted icon means the panel is on the window; clicking closes it or brings it back **to the last place it was**.
- **Panel position** — for each open panel: *Move to* (every cell of the grid, "Row 1, col 2"…), *Split this group*, *Float*, *Collapse to the side* (edge cells only), *Close*.
- **Maximize panel** and **Rearrange panels**.
- **Perspectives** — ready-made arrangements and your own.
- **Restore layout** — puts every panel back in its default place.

If you close every panel the window shows *No panels* with a **Restore layout** button. Settings → Layout has the same button.

## Moving panels

1. **View → Rearrange panels** is on by default: every cell shows its tab strip. Turn it off to hide the strip of cells that have a single panel (the choice is remembered).
2. **Drag a tab** to another cell. While you drag, the **whole grid** shows as dashed cells — empty ones included — and a **Float** pill in the top-right corner of the board. Dropping on an empty cell fills it (the neighbours give up the space they had absorbed); dropping on a filled cell makes it a tab there; dropping between the tabs inserts it at that position.
3. Drag a tab **out of the window** and it becomes a [floating window](#floating-windows).

Moving a panel **never recreates it**: a running command, its tabs and the text in the Output survive any move. The tab menu (right click, or the **⋯** button) offers the same moves without dragging.

## Closing, maximizing, collapsing

- **Close** a panel from the right click menu of its tab or the View menu. Commands and Output can be closed too — running a command brings the Output back.
- **Maximize** a panel with the maximize icon next to the **⋯** on its tab bar (it becomes *restore* while maximized), a double click on its tab, or **Ctrl+Shift+M**: the other cells step aside. The old *focus mode* is just the Output maximized. The Output has no collapse button. Moving or revealing another panel leaves it.
- **Collapse to the side**: a cell on the **edge** of the grid, collapsed from its tab menu, becomes a thin strip of icons on that edge (left/right in vertical fill, top/bottom in horizontal fill; cells in the middle do not collapse). Several collapsed cells of the same edge share one strip. Click an icon and the panel opens **beside** the content, pushing it aside (nothing is covered); **drag the divider** to resize it (the size is the cell's, so expanding it back keeps it); the strip's icons already switch panels, so the opened cell has no tab strip of its own; click the icon again and it closes. Dropping a panel onto a collapsed cell keeps it collapsed: the panel joins the strip and opens beside the content. The chevron at the start of the strip expands its cells back. The panel that was open beside the content comes back open after a restart. Good for small windows.

## Split mode, drop targets and ordering

- **Split this group** (panel menu, for a cell with two or more panels): instead of tabs the cell shows **all** its panels at once, each in its own box with a header — side by side in a wide cell, stacked in a narrow one (it follows the shape of the cell). Drag a header to reorder or move a panel; **Back to tabs** undoes it. Example: the command tree and the AI chat together in the first cell.
- While dragging a panel, the dashed cells show where it can land and a **Float** pill in the top-right corner of the board: drop on it and the panel becomes a floating window.
- **Reordering**: drop a tab between other tabs, a split box header before/after another, or an icon between the icons of a collapsed strip — the panel lands exactly where you dropped it.
- Resizing is free: dividers go down to a few pixels and no panel imposes a minimum height on its cell (a panel taller than its cell is cropped, or scrolls — see below).

## Small windows: scroll, don't squeeze

Each cell has a comfortable size (about 300 × 180 px) and the form panels (Variables, Processes, History, Notifications, AI chat) a width they need to stay readable. When the window is smaller than what the visible cells ask for, the **board scrolls** (horizontally and vertically) instead of crushing them. Dragging a divider to make a cell smaller than comfortable is still yours to do: a form panel narrower than its size scrolls inside its card. The strip of command buttons scrolls sideways when the list is narrow. Panels that scroll on their own (the tree, the Output, the Document) never get a second scrollbar around them.

## Floating windows

**Float** (tab menu) or drag a tab out of the window: the panel gets its own window with an **always on top** pin and a button to **dock back**. On Windows it uses the **native window frame**, so you can drag it to a screen edge or corner to snap it, use **Win+arrows**, and get the Windows 11 snap layouts; elsewhere it keeps the Kai frame. The detached Output window works the same way.

On **Windows** the main window also behaves like a native one even though Kai draws its own title bar: drag it to a screen edge or corner to snap it, use **Win+arrows**, resize from any edge or corner, drag the bar down from a maximized window to restore it, and double click the bar to maximize.

Floating windows are independent windows (clicking one — pinned on top or not — does not bring the main Kai window forward), but they follow Kai: **hiding Kai hides them, and they only come back when Kai comes back** — they never appear by themselves, not at boot, not after a monitor change. Closing a floating window closes the panel (View brings it back). On Wayland the window manager decides where a window goes, so only the size is restored.

## Perspectives

**View → Perspectives** switches the whole arrangement at once:

| | |
|---|---|
| **Runner** | the default, 2 × 2: commands left, Output right |
| **Classic** | the old arrangement, 1 × 2: commands on top, Output below |
| **Compact** | only the command list; the Output comes back when you run something |
| **HTTP** | commands and a wide Output on the right |
| **Reading** | commands alone on the left, the Document on top of the Output on the right |
| **Triple** | 3 × 1: commands, Output and Document side by side |
| **Quad** | 2 × 2: commands, Output, Document and Processes, one cell each |
| Yours | *Save current layout…* names the arrangement you have now; they appear below the built-in ones and can be deleted |

Switching perspective is always your action: Kai never changes the layout on its own.

## Runs side by side

Drag a tab **out of the Output's tab bar** (or right click it → **Open in a panel**) and drop it on a cell: that run gets its **own panel** with the log, response and state of *that* command, while the Output shows another. Two logs, backend and frontend, visible at once. While the command has a panel it lives **only** there: its tab leaves the Output, and running it again does not open it in the Output or take the front there. Closing the panel gives the command back to the Output (its tab returns, without taking the front). Exception: a command with an *interactive terminal* needs the Output's terminal, so its tab stays there and the panel is only a mirror. The panel has its own answer field for commands that read input. The panel header has its own **Run / Stop / Force stop / Reset** buttons (and *Quick edit* for HTTP) for that command, and the log keeps the command's [formatted output](output.md). The detached Output window has the same buttons. Run panels last for the session only. The tab of a [KIP](kip.md) app becomes the app's own panel (see below).

## Pinning a KIP command

Right click a [KIP](kip.md) command → **Pin as panel**, drag its tab out of the Output, or right click the tab → **Open in a panel**. The app becomes a permanent mini-app of the window: its screen is the panel and the panel keeps the command's icon. It comes back after a restart and, with **Open with Kai** on (the default when you put it there; right click the panel's tab to turn it off), it opens by itself on its first screen — a KIP app only waits for you, nothing goes ahead without an answer. Opening by itself is quiet: the panel stays where the layout left it and does not take the keyboard focus until you click into it. With the option off, or when the app needs a parameters form, the panel shows a card with a **Run** button.

While the app has a panel it lives **only** there, like a tab of its own: the Output gets no tab for it and doesn't switch to it when you run it (Run again, the tree, the CLI) or select it in the tree — the panel comes to the front instead. Closing the panel gives the app back to the Output (its tab returns, without taking the front). Deleting the command removes the panel.

## Keyboard

All remappable in **Settings → Shortcuts**:

| | |
|---|---|
| `Alt+1` … `Alt+9` | focus the 1st … 9th visible panel (in screen order: column by column, or row by row in horizontal fill; floating last) |
| `F6` / `Shift+F6` | next / previous panel |
| `Ctrl+Shift+M` | maximize or restore the panel that has the keyboard |
| *(unset)* | rearrange panels · restore layout |

## Where it is stored

`layout.json` in your config folder (`~/.config/kai/` on Linux, `%APPDATA%\Kai\` on Windows) — **personal, never exported** and never part of a project's `kai.yml`. It is written in the background; a damaged file is kept next to it as `layout.json.corrupt-…` and Kai starts with the default layout. A file from before the grid (five fixed zones) is not converted: Kai starts with the default layout and keeps the old file as `layout.json.pre-grid`; perspectives saved in that format are dropped. Panels that no longer exist (an uninstalled provider, a deleted command) are dropped when it is read. The old *Output position* setting migrates into it the first time (*left* is kept; *bottom* — the old default — and *right* become the new default).

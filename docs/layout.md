# Panels & layout

The Kai window is a board of **panels** that you can move, close and bring back — a bit like VS Code or JetBrains, but light. The default layout has the **command list on the left** and the **Output on the right**, half the window each, both with their tab strip showing (so every panel can be dragged right away). It is what you see until you change something; the old arrangement (list on top, Output below) is the **Classic** perspective.

## The four zones

Panels live in four **zones**: **Left**, **Center**, **Right** and **Bottom**. A zone with more than one panel shows them as tabs; a zone with none disappears. By default: *Commands* on the left, *Output* on the right.

| Panel | What it is | Opens in |
|---|---|---|
| **Commands** | the tree, its search and action buttons | left |
| **Output** | the tabs of what ran, with logs and responses | right |
| **Document** | the README / notes reader, so you can read it **while a command runs** | right |
| **Processes** | what is running now, with stop / force stop | right |
| **History** | the run history, with re-run | right |
| **Notifications** | the notification center | right |
| **Variables** | the dynamic variables, with edit and reset | right |
| **AI chat** | the assistant's chat — **only exists once you add a provider** | right |
| **Run panels** and **pinned KIP commands** | see below | right |

Everything that used to open as a modal window (processes, history, notifications, the AI chat) is now one of these panels: the same screen, but docked wherever you want it.

## The View menu

**View** in the top bar is the way back to everything:

- **The panel list** — a highlighted icon means the panel is on the window; clicking closes it or brings it back **to the last place it was**.
- **Panel position** — for each open panel: *Move to* Left / Center / Right / Bottom, *Float*, *Maximize*, *Collapse to the side*, *Close*.
- **Maximize panel** and **Rearrange panels**.
- **Perspectives** — ready-made arrangements and your own.
- **Restore layout** — puts every panel back in its default place.

If you close every panel the window shows *No panels* with a **Restore layout** button. Settings → Layout has the same button.

## Moving panels

1. **View → Rearrange panels** is on by default: every zone shows its tab strip. Turn it off to hide the strip of zones that have a single panel (the choice is remembered).
2. **Drag a tab** to another zone. While you drag, four dashed regions always show where it can land — the same ones whatever the layout: **left** and **right** (full height), **bottom** and **center**. Dropping between the tabs of a zone inserts it at that position.
3. Drag a tab **out of the window** and it becomes a [floating window](#floating-windows).

Without a **Center** panel, the **Bottom** zone sits under the left and right zones across the whole width (with a Center panel it sits under the Center only).

Moving a panel **never recreates it**: a running command, its tabs and the text in the Output survive any move. The tab menu (right click, or the **⋯** button) offers the same moves without dragging.

## Closing, maximizing, collapsing

- **Close** a panel with the × on its tab or the View menu. Commands and Output can be closed too — running a command brings the Output back.
- **Maximize** a panel with a double click on its tab, the menu, or **Ctrl+Shift+M**: the other zones step aside. The old *focus mode* (the chevron at the start of the output bar) is just the Output maximized. Moving or revealing another panel leaves it.
- **Collapse to the side**: a zone collapsed from its tab menu becomes a thin strip of icons on the window edge. Click an icon and the panel opens **beside** the content, pushing it aside (nothing is covered); **drag the divider** to resize it (the size is the zone's, so expanding the zone back keeps it); the strip's icons already switch panels, so the opened zone has no tab strip of its own; click the icon again and it closes. Dropping a panel onto a collapsed zone keeps it collapsed: the panel joins the strip and opens beside the content. The chevron at the start of the strip expands the zone back. Good for small windows.

## Floating windows

**Float** (tab menu) or drag a tab out of the window: the panel gets its own window, with the Kai look, an **always on top** pin and a button to **dock back**.

Floating windows belong to the main window: **hiding Kai hides them, and they only come back when Kai comes back** — they never appear by themselves, not at boot, not after a monitor change. Closing a floating window closes the panel (View brings it back). On Wayland the window manager decides where a window goes, so only the size is restored.

## Perspectives

**View → Perspectives** switches the whole arrangement at once:

| | |
|---|---|
| **Runner** | the default: commands left, Output right |
| **Classic** | the old arrangement: commands in the center, Output below |
| **Compact** | only the command list; the Output comes back when you run something |
| **HTTP** | commands and a wide Output on the right |
| **Reading** | commands on the left, the Document in the center, the Output below |
| Yours | *Save current layout…* names the arrangement you have now; they appear below the built-in ones and can be deleted |

Switching perspective is always your action: Kai never changes the layout on its own.

## Runs side by side

Drag a tab **out of the Output's tab bar** (or right click it → **Open in a panel**) and drop it on a zone: that run gets its **own panel**, a live mirror of the command (log, response, state), which keeps following *that* command while the Output shows another. Two logs, backend and frontend, visible at once. The tab stays in the Output; closing the panel just discards the mirror. Run panels last for the session only.

## Pinning a KIP command

Right click a [KIP](kip.md) command → **Pin as panel**. The command becomes a permanent mini-app of the window: its screen is the panel. Until you run it, the panel shows a card with a **Run** button — **nothing ever runs on its own**. The pinned panel comes back after a restart, and the Output (which can only show one live view per session) shows "Open elsewhere" while the panel exists. Deleting the command removes the panel.

## Keyboard

All remappable in **Settings → Shortcuts**:

| | |
|---|---|
| `Alt+1` … `Alt+9` | focus the 1st … 9th visible panel (left, center, right, bottom, floating) |
| `F6` / `Shift+F6` | next / previous panel |
| `Ctrl+Shift+M` | maximize or restore the panel that has the keyboard |
| *(unset)* | rearrange panels · restore layout |

## Where it is stored

`layout.json` in your config folder (`~/.config/kai/` on Linux, `%APPDATA%\Kai\` on Windows) — **personal, never exported** and never part of a project's `kai.yml`. It is written in the background; a damaged file is kept next to it as `layout.json.corrupt-…` and Kai starts with the default layout. Panels that no longer exist (an uninstalled provider, a deleted command) are dropped when it is read. The old *Output position* setting migrates into it the first time (*left* is kept; *bottom* — the old default — and *right* become the new default).

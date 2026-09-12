# Spec 09: Top Utility Bar

## 1. Overview
Kai uses the OS's native window frame and controls (see spec 05 §1) — no
custom/frameless window. What this spec covers is the **Top Utility Bar**,
a CopyQ-inspired header providing quick access to settings, help, process
management and window actions.

## 2. Layout

```text
+-----------------------------------------------------------------------------------------+
| [logo]  [☰ Menu]                                                                          |
+-----------------------------------------------------------------------------------------+
```

`TopUtilityBar` shows the app logo (falls back to the text "Kai", or the
default Qt icon for the window/tray, if `assets/logo/kai.png` isn't
found — never crashes over a missing asset) and a single **"☰ Menu"**
button.

## 3. Menu entries

| Entry | Action |
| :--- | :--- |
| Import Project | Opens the native folder picker (`ProjectSelector::promptForDirectory`) to import a `kai.json`, or (with generic detection on) a project without one. |
| Processes | Opens `ProcessListDialog`, listing active background processes. |
| *(separator)* | — |
| Settings | Opens the Global Settings dialog (theme, per-action configurable shortcuts, appearance, window behavior). |
| Logs | Opens `LogViewerDialog` (non-modal), showing the app's own internal log history (`utils::Logger`), filterable by level. |
| Help | Opens the built-in help (keyboard shortcuts, `{{VAR}}` syntax, a link to the `kai.json` reference). |
| *(separator)* | — |
| Hide | Hides the window without quitting (`MainWindow::toggleVisibility`) — same effect as the global shortcut. |
| Quit | Actually quits the app (`MainWindow::quitApplication` → `QCoreApplication::quit`, background processes stopped safely via `aboutToQuit`). Distinct from the title bar's "X", which only hides the window (same as "Hide") — see spec 05 §1. |

## 4. New Folder / New Command

These two actions live in the **action sidebar** next to the command tree,
not in the Top Bar menu — they're always enabled (independent of any
selection) and pre-fill their target folder from the current selection or
active tab.

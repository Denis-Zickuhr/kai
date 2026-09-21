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

## Window corners follow the corner preference

The window corners use the `radiusMd` token (a mask is binary, so a smaller
radius hides the jaggies; ~8-9px matches Windows 11) (Settings → Appearance → Corners:
sharp = square, soft/rounded = rounded). The frameless window is **translucent**
(`WA_TranslucentBackground`, set before the native window exists): the
`QMainWindow` paints nothing and only the `rootContainer` paints its background
with `border-radius`, which Qt antialiases, so the corners are smooth. The top
bar gets the same top radii so its square background can't leak past the arc.

Not every environment honors window transparency (the corner then stays
square), so `MainWindow::updateWindowShape()` ALSO applies a rounded **mask**
whenever the radius is > 0 — binary, so jagged where it is the only thing
rounding the corner. When translucency is on, the mask radius is 2px smaller
than the painted one: the mask sits outside the smooth arc and doesn't jag it.
Headless platforms (offscreen/minimal — tests/CI) and
`KAI_NO_TRANSLUCENT_WINDOW=1` (e.g. bare X11 without a compositor, where
translucency turns into a black background) skip translucency and use the
full-radius mask alone. Maximized/fullscreen windows have no mask and the
`rootContainer` radius is 0 there (`[kaiMaximized="true"]`).

### Windows 11: native DWM corners

On Windows the corners come from the DWM itself
(`DWMWA_WINDOW_CORNER_PREFERENCE`, applied by `applyNativeWindowCorners` once the
native window exists and again whenever the corner style changes): smooth,
antialiased, identical to standard Windows 11 windows. Sharp = do not round;
soft/rounded = `DWMWCP_ROUND` (the DWM offers a single large radius, so soft and
rounded look the same on the window frame). The 1px DWM border takes the theme's
border color. No translucency, no mask, and the `rootContainer` radius is 0
(the DWM already clips the window). If the DWM rejects the attribute
(Windows 10) the rounded mask above is used as a fallback.

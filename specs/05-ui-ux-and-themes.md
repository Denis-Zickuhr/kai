# Spec 05: UI/UX & Theme System

## 1. Window & layout

- **Window frame — always drawn by Kai:** no window in the app uses the OS
  title bar. The main window, the detached output window (`AppWindowFrame`)
  and **every dialog** (`QDialog`, including `QMessageBox`/`QInputDialog`;
  `installDialogFrames()` in `ui/shared/dialog-frame.*`) share the same look:
  a Kai title bar (logo, title, window buttons — only Close on dialogs),
  a contrasting 1px border plus a soft inner shadow (`DialogEdge`, an
  overlay that ignores the mouse), corners from the user's corner preference
  (`radiusMd`; DWM native corners on Windows 11, a region mask elsewhere),
  drag by the title bar and resize by the edges. Dialogs are never
  translucent: enabling `WA_TranslucentBackground` after the native window
  exists (and on WSLg, which does not honor alpha windows) left the
  background full of garbage pixels, so the separation from the window behind
  comes from the inner edge, not from an outer drop shadow. Dialogs get it without per-dialog code: the filter
  sets `FramelessWindowHint` on the first polish, and on first show adds the
  title bar as an overlay child and grows the layout's top margin by its
  height (works with any layout type). OS-native pickers (file/folder
  dialogs) are the only exception — they are not Qt widgets. See spec 09 for
  the Top Utility Bar.
- **Resizable panes via `QSplitter`:** the command tree, the action
  sidebar, and the output panel sit inside two `QSplitter`s (a horizontal
  one between the tree and the action sidebar, nested inside a vertical
  one alongside the output panel). `setChildrenCollapsible(false)` on both
  keeps the user from accidentally losing the tree or the output panel by
  dragging a divider all the way.
- **Resizable dialogs:** every `QDialog` in Kai (command/folder editor,
  settings, help, log viewer, process list, parameter form, icon picker,
  ...) has `setSizeGripEnabled(true)`.
- **Project selector:** `QFileDialog::getExistingDirectory` picks a folder
  (or points at a `kai.json`) to import as a normal folder in the command
  tree.
- **Fuzzy search bar:** hidden by default, toggled by a remappable
  shortcut (`Ctrl+F`); autofocuses when shown, clears and returns focus to
  the tree when hidden. Down-arrow from the field jumps to the first
  visible item in the active tree (CopyQ/Spotlight-style keyboard
  navigation).
- **Circular tab navigation:** left/right arrow shortcuts move between
  root-folder tabs as a circular list (past the last tab wraps to the
  first, and back). Remappable in Settings; recalculated as folders
  change.
- **Configurable shortcuts for every action:** centralized in
  `SettingsData`, reloaded immediately when Settings are saved (no
  restart). Item-scoped actions (Edit `F2`, Delete `Delete`) only fire
  with the command tree focused (`Qt::WidgetWithChildrenShortcut`);
  window-scoped ones (New Folder, New Command, Quit) use
  `Qt::WindowShortcut`.
- **Shortcut capture field:** a read-only field that, on focus, captures
  the next key combination pressed and turns it into a `QKeySequence`
  automatically — no manual string typing.
- **Window semantics:** the title bar's "X" and the "Hide" action (menu or
  global shortcut) both just hide the window (`hide()`) and keep the
  process running; neither quits the app. Quitting for real is "Quit"
  (`Ctrl+Q` → `QCoreApplication::quit`, with background processes stopped
  safely via `aboutToQuit`).
- **Startup visibility:** `SettingsData::startVisible` decides whether the
  window shows up on boot, independent of the global shortcut (see spec
  02 §4.2).

## 2. Command tree & tabs

- Every root folder (`parent_id == null`) becomes its own tab, using its
  persisted icon and name. Subfolders stay nested inside that tab's tree.
  A folder whose parent no longer exists is promoted to a root tab instead
  of disappearing silently.
- Each tab is a `QTreeWidget`: folders start collapsed, Enter/double-click
  toggles expand/collapse, and each row has inline controls — Play/Stop,
  Edit, Delete — plus a small document-icon button for HTTP commands to
  jump straight to editing the body.
- A running command gets a small red dot overlaid on its icon
  (`m_runningCommandIds`), instead of a separate status column.
- **Command creation/edit mode:** a global Settings choice between
  **Standard** (the full form dialog) and **Advanced** (raw JSON editing
  via `Command::toJson`/`fromJson`) for both creating and editing a
  command.

## 3. Output panel

- Always present in the layout (never removed from the splitter) — only
  its content is cleared when there's nothing to show.
- Tabs: **Response** (JSON tree for HTTP), **Request** (what was actually
  sent — method, URL, headers, body), **Output** (raw stdout/stderr,
  hidden for HTTP commands), **Headers**, **Env** (the effective resolved
  variables for that run). Which tabs are visible adapts to the command
  type and what actually has data.
- A command with `interactive_terminal` set swaps the tabbed body for a
  real terminal grid (libvterm) instead — see spec 03 §1.
- A status badge (`● Running` / `● Done` / `● Failed` / `● Background`)
  sits next to the connected command's name. Single-run commands stay
  visible after they finish; the user dismisses them manually.
- Collapsing the panel when it's docked to the left or right side
  collapses its **width**, anchored to the top — not its height, and never
  centered.
- Collapsed, the header keeps **only the chevron** (tabs, actions and status
  badge are hidden) so the command box gets the space back.
- One tab style everywhere (`ui/shared/tab-bar-style.h`): same height (bar
  height = control height + 2 grid steps), horizontal padding, font size and
  accent underline for the output tabs, the root-folder tabs and every other
  `QTabBar`. Tables inside rounded cards are inset by ~30% of the corner radius
  so their striped rows never spill over the rounded corners.
- The root-folder tab strip has its own background (`surface2`, full width)
  so it contrasts with the list below (`TabStripBackground`).
- Panel frames (`QWidget#panelCard`) are drawn by QSS. Their children are
  inset by `panelFrameInset()` (1px border + ~30% of the corner radius + 1px),
  which keeps the square corner of every child inside the border arc so it can
  never paint over it; re-applied when the corner style changes. (An overlay
  attempt, `PanelFrame`, blanked the panels and is not attached.)
- Root-folder tab strip background: slightly darker than the default window
  background (`bg` darkened), not a lighter surface color.
- Welcome screen: only the welcome screen is shown — the command list, every
  action bar (top/bottom/sides) and the output panel are hidden; closing it
  restores them according to the user's preferences.
- Tab overflow: when the root-folder tabs (command list) or the output header
  (tabs + action icons) don't fit, a thin bar in the theme accent color
  (slightly translucent) shows below the labels and icons, with how much is
  visible and where (`OverflowIndicator`). It is a real scroll bar: drag the
  thumb or click the track to scroll. It only exists while there is overflow;
  in the output header the tab/icon row shrinks by the bar's height so the
  selected tab's underline never touches it. The output header scrolls
  horizontally (mouse wheel); the collapse chevron stays pinned outside the
  scrolling row.
- Look (JetBrains tool-window style): flat header — plain-text tabs with an
  accent underline on the selected one, flat action icons on the right (no
  boxed group), a separator line above the content.
- The command box and the output panel sit inside the same frame
  (`QWidget#panelCard`: 1px border + `radiusMd` token, rule in
  `app-stylesheet.cpp`); only the border/surroundings are shared, each
  keeps its own interior color.
- ANSI colors are rendered in Output; printed `http(s)://` URLs are
  detected, underlined, and clickable (opens in the system's default
  browser).
- **Output bar** (`OutputTabsBar`, `src/ui/features/output/output-tabs-bar.*`, owned by `TerminalDrawer`, wired in
  `MainWindow::refreshOutputTabs/activateOutputTab/closeOutputTab/showOutputTabMenu/cycleOutputTab`): a flat strip
  (no island, 1px separator under it, same height as the neighbouring bars) above the panel with ONE tab per command
  that ran in the session (folder-action runs: `Name · Folder`, one per folder; ids are the real command id or the
  virtual `act:<cmd>|<folder>`). A tab = status dot (Running/Waiting for a KIP answer/Success/Failed/Skipped), name,
  elapsed time while it runs (1s timer only while something runs) and an accent dot for output that arrived while the
  tab was not in focus (`appendToCommandLog` -> `markActivity`). The × shows on hover and on the tab in focus.
  - Tabs come from `m_outputTabOrder`: run start (`prepareDrawerForRun`) and showing an output (`reconnectTerminalToCommand`
    when there is something to show) add one; the output on screen always has one. Closing (×, middle click, menu)
    only removes the tab — the process and `m_commandLogs` stay; clicking the command in the tree or running it again
    brings the tab back. Closing the tab in focus activates its neighbour; closing the last clears the panel
    (`clearOutputPanel`). Not persisted.
  - Click = select the command in the tree (switching the root-folder tab) and reconnect the output; for an action,
    select its folder, reconnect and focus the icon. Right click first activates the tab, then shows: Stop, Run again,
    Show in the command tree, Close / Close others / Close finished, a separator and
    `OutputPanel::fillContextMenu` (clear, copy all, export + the display options — the same items as the options
    button). Overflow: wheel scrolls, a button lists every tab. Shortcuts `action.next_output` / `action.previous_output`
    (Ctrl+Tab / Ctrl+Shift+Tab, rebindable).
  - The bar is ALWAYS present (empty: a muted hint, `output_tabs.empty`) and only hides while the panel is collapsed (`setStripAllowed`). The overflow button is painted by the bar itself (a `QToolButton` was deformed by the global stylesheet). Right click does NOT activate the tab: the menu's clear/copy/export act on the clicked tab's `m_commandLogs`; only "Show in the command tree" activates it.
- Folder-action icons on hover (`FolderActionsView::hoverGlyph`): an **eye** when the next click only shows an output
  (running, or it ran and is not loaded), a **play** when the click runs again (loaded and stopped), nothing special
  when it never ran.
- Clicking the already-selected command row re-emits `CommandTreeWidget::commandClicked`; `MainWindow` reconnects the
  output if it shows another command (a KIP started from search/shortcut used to leave the first click without effect).

## 4. Dialogs & forms

- **Where a dialog opens** (`dialog-utils.h`): centered on the Kai window the user is USING — `centerReferenceFor` picks
  the app's active window (visible, not minimized, not the dialog itself), then the parent's window, then the screen under
  the cursor — not on the parent. With the detached output / KIP window on another monitor the parent is the main window,
  and centering on it sent dialogs to the wrong screen. The choice is remembered across the Show and the `singleShot(0)`
  re-center (`CenterReference`), and `MainWindow::eventFilter` applies it to EVERY top-level `QDialog` shown
  (`QMessageBox`, `QInputDialog`, ...) that does not already call `centerOnParent`.

- The command and folder editors group related fields into `QGroupBox`
  sections (Identification, Parameters, Hooks, Execution Condition,
  Headers, Body, Env Extractors) instead of loose fields.
- A folder picker (searchable combo, indented by depth) is reused
  everywhere a folder needs picking: command/folder parent, project
  import destination.
- **Icons:** picked through a modal `IconPickerDialog` (a grid of icons,
  `QListWidget::IconMode`), not a `QComboBox` — the pool is drawn via
  `QPainter` with simple geometric shapes, so it never depends on an emoji
  font being installed. A "Choose file..." option lets a command or folder
  use a custom image (PNG/JPG/SVG/ICO) instead, stored as
  `"file:<absolute path>"`.

## 5. Process list & logs

- **Process List** (`ProcessListDialog`, Processes menu): every tracked
  background process, with a colored status dot, its accumulated log, and
  a button to stop it.
- **Log Viewer** (`LogViewerDialog`, Processes menu): a rolling view (up to
  2000 entries) of `utils::Logger`'s own output, filterable by minimum
  level (Debug/Info/Warning/Error). Non-modal.

## 6. Theme system (CopyQ-inspired)

### 6.1. File format
- Themes are **`.json`** files under `~/.config/kai/themes/*.json`.
- Each file is a complete, independent theme (no inheritance between
  files).
- Kai ships `dracula.json` (default dark theme) and `light.json`.
  `MainWindow::ensureDefaultThemeInstalled` installs/updates every bundled
  theme, keyed by a per-theme `schema_version` field — an installed
  built-in theme is silently upgraded when the bundled version is newer,
  but a user's own theme (or a copy saved under a different name) is
  never touched.

### 6.2. Base variables
The minimum palette every theme should declare in `"variables"`:
- `bg`, `alt_bg` — primary/secondary background.
- `fg`, `alt_fg` — primary/secondary text color.
- `sel_bg`, `sel_fg` — selected-item background/text.
- `accent_color` — accent (focus borders, primary buttons).
- `border_radius` — default corner radius (e.g. `"4px"`).
- `font_family`, `font_size` — default typography.

Extra variables can be declared freely (e.g. `edit_bg`, `notes_fg`) and
referenced from component rules.

### 6.3. Color expressions
Referencing a variable inside a rule allows color arithmetic:
```
${variable_name [+|-] #rrggbb}
```
Example: `${bg - #222222}` darkens `bg` by subtracting the hex value
component-wise (clamped to `0x00`-`0xff` per channel). `${sel_bg +
#101010}` lightens it. Expressions can be chained: `${fg - #044 + #400}`.

This derives hover/disabled/unselected-tab variants without duplicating
color values in the theme file.

### 6.4. Component rules
Beyond the base variables, a theme can declare per-component CSS blocks
under `"components"`, each interpolated with the variables/expressions
above and translated to QSS on load:
- `main_window`, `menu_bar`, `menu`, `tool_bar`, `tool_button`,
  `tool_button_hover`, `tool_button_pressed`
- `tab_bar`, `tab_selected`, `tab_unselected`
- `search_bar`, `search_bar_focused`
- `item`, `item_hover`, `item_selected`
- `notification`, `tooltip`

Each value is a string of CSS properties (no selector), e.g.:
```json
"tab_selected": "background: ${bg}; border: 1px solid ${bg}; color: ${fg}; padding: 0.5em;"
```
Components left out fall back to a default derived from the base
variables, covering `QDialog`, `QToolButton` (normal/hover/pressed/
checked), `QTabWidget::pane`, `QCheckBox::indicator`,
`QHeaderView::section`, `QScrollBar` and `QComboBox`.

### 6.5. Example theme file
```json
{
  "name": "dracula",
  "schema_version": 4,
  "variables": {
    "bg": "#282a36",
    "alt_bg": "#21222c",
    "fg": "#f8f8f2",
    "alt_fg": "#d6acff",
    "sel_bg": "#44475a",
    "sel_fg": "#f8f8f2",
    "accent_color": "#bd93f9",
    "border_radius": "4px",
    "font_family": "Ubuntu",
    "font_size": "12pt"
  },
  "components": {
    "main_window": "background: ${bg}; color: ${fg};",
    "tab_bar": "background: ${bg - #222222};",
    "tab_selected": "background: ${bg}; border: 1px solid ${bg}; color: ${fg}; padding: 0.5em;",
    "tab_unselected": "background: ${bg - #222222}; border: 1px solid ${bg}; color: ${fg - #333333}; padding: 0.5em;",
    "tool_button_pressed": "background: ${sel_bg};",
    "search_bar_focused": "border: 1px solid ${sel_bg};",
    "item_selected": "background: ${sel_bg}; color: ${sel_fg}; border-radius: 2px;"
  }
}
```

### 6.6. Live reload
- `ThemeManager` watches the active theme file via `QFileSystemWatcher`.
- On a change, it reprocesses variables + color expressions + components
  and reapplies the resulting QSS at runtime, without restarting the app.
- A parse error (invalid JSON or a malformed color expression) never
  crashes the app: the previous theme stays loaded and a warning is
  logged.

# Spec 05: UI/UX & Theme System

## 1. Window & layout

- **Main window:** uses the OS's native title bar and window controls
  (minimize/maximize/close) — see spec 09 for the complementary Top
  Utility Bar. Kai does not use a custom frameless window; that keeps it
  familiar and consistent with native window management on Linux and
  Windows.
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
- ANSI colors are rendered in Output; printed `http(s)://` URLs are
  detected, underlined, and clickable (opens in the system's default
  browser).

## 4. Dialogs & forms

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

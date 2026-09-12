# Spec 07: Error Handling, Resilience & Edge Cases

## 1. Persistence failure recovery
- **Corrupted JSON files:** if `commands.json` or `settings.json` fails to
  parse, Kai:
  1. Creates a backup of the corrupted file (`commands.json.bak.[TIMESTAMP]`).
  2. Falls back to a safe empty state.
  3. Shows a discreet UI notification about the recovery.
- **Atomic writes:** data is always saved to a temporary file first
  (`commands.json.tmp`) and then renamed into place, to avoid data loss on
  an OS-level crash mid-write.

## 2. Execution engine resilience
- **Async signal handling:** status changes coming from `QProcess`
  signals are dispatched via `QMetaObject::invokeMethod(...,
  Qt::QueuedConnection)`, deferred to the next event-loop cycle — never
  handled synchronously inside the originating signal's own call stack,
  which avoids reentrancy issues when the UI reacts by rebuilding widgets.
- **Zombie/stuck processes:** every background or terminal-driven
  `QProcess` is torn down safely on `aboutToQuit` (`terminate` → timeout →
  `kill`). Pre/Post hooks have a configurable timeout (default 30s).
- **Missing environment variables:** an unresolved `{{VAR}}` becomes an
  empty string `""` plus a warning in the output panel — never a crash.

## 3. Widget stability under Wayland/WSLg
Kai targets X11 (`xcb`) by default when a `DISPLAY` is available and the
platform isn't explicitly pinned, since some WSLg/Wayland configurations
have incomplete GPU/EGL support that can leave the window invisible or
cause instability in native widgets such as `QComboBox` popups and
`QTableWidget`'s cell editor. Concretely:

- Editable tables (`KeyValueEditorWidget`, `ParameterEditorWidget`) never
  use `QTableWidgetItem`'s native inline editor — every cell is a real
  embedded widget (`QLineEdit`/`QComboBox` via `setCellWidget`), with
  `QAbstractItemView::NoEditTriggers` on the table.
- `IconPickerWidget` uses a full modal dialog (`IconPickerDialog`) instead
  of a `QComboBox` popup for the same reason.
- `QT_IM_MODULE` is cleared before creating the `QApplication` to avoid a
  known Wayland/WSLg dead-keys issue where plain characters like `/` and
  `'` get composed into accented characters. Escape hatch:
  `KAI_KEEP_IM_MODULE`.
- `QT_QPA_PLATFORM=xcb` is preferred over Wayland when a `DISPLAY` is
  present and the user hasn't pinned a platform. Escape hatch:
  `QT_QPA_PLATFORM=wayland`. Headless environments (offscreen, used by
  tests/CI) pin their own platform and are unaffected.
- Known, environment-level limitation (not fixable from Kai's side): WSLg
  can misreport display DPI after a monitor/scaling change, making the
  window appear oversized until fully closed (including from the tray)
  and reopened.

## 4. HTTP client resilience
- **Network failures / timeouts:** a 10-second default request timeout on
  `QNetworkAccessManager`.
- Non-2xx responses (4xx/5xx) count as a pipeline failure when the request
  is a Pre-Hook.
- **JSON extractor failure:** if the configured `json_path` doesn't exist
  in the response payload, a warning is logged and the target
  environment variable is left unchanged (never overwritten with garbage).

## 5. Folder hierarchy resilience
- A folder whose `parent_id` points at a folder that no longer exists is
  never dropped silently — it's promoted to a root-level tab, staying
  visible and usable.

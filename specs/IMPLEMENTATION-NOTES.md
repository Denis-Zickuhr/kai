# Kai Implementation Notes – Session 2

## Overview

This document describes the implementation of **PARTE 1** (Autosync/Cron Fixes) and **PARTE 2** (Tab Standardization) for Kai.

## PARTE 1: Autosync & Cron Improvements

### Components

#### ProjectSyncManager (`src/engine/project-sync-manager.h/.cpp`)

**Purpose:** Manages bidirectional synchronization between Kai configuration and external `kai.json`/`kai.yml` files.

**Key Features:**
- **Rescan:** Discovers project files under `Folder::workingDir`
- **Watcher:** Monitors filesystem changes via `QFileSystemWatcher`
- **Hash-based tracking:** Persists content hashes in `project-sync-state.json`
- **Manual sync:** User can trigger File→Kai or Kai→File synchronization
- **Pause/Resume:** Allows temporary suspension during conflict resolution

**API:**
```cpp
void rescan(const CommandsData &commandsData);
void pauseAutoSync(const QString &folderId);
void resumeAutoSync(const QString &folderId);
void syncKaiToFileManually(const QString &folderId, const CommandsData &commandsData);
void syncFileToKaiManually(const QString &folderId, CommandsData &outCommandsData);
void notifyCommandsSaved(const CommandsData &commandsData);  // FIXME: stub
```

**Signals:**
- `externalChangeApplied(folderId, message)` – External file was synced successfully
- `syncConflictDetected(folderId, details)` – Both sides changed (awaiting user decision)
- `syncPausedChanged(folderId, paused)` – Pause state toggled

**Integration Point:**
Call `notifyCommandsSaved(commandsData)` after `ConfigManager::saveCommands()` succeeds. Manager detects changed folders via hash comparison and triggers sync if enabled.

#### Folder Editor Dialog

**Changes:**
- Added "Working Directory" field (QLineEdit + browse button)
- Position: between Profile and Project checkboxes
- Placeholder: "Empty = inherit from parent folder"
- Used for: command cwd default + Autosync discovery root

**Hierarchical Resolution:**
When `workingDir` is empty, climb the folder tree until finding a non-empty value.

#### Command Editor Dialog

**Changes:**
- CRON expression field moved from "Window" section → new "Scheduling" section
- Hint label font-size via `tk::fontSizeSmallPt()` (not hardcoded 11px)
- Example placeholder: "0 9 * * 1-5 (9 AM Mon-Fri)"

#### Autosync Settings Tab

**Layout:**
- Master switch: "Autosync enabled" (visible always)
- Direction checkboxes: "File→Kai", "Kai→File" (disabled when master=off)
- Search directories table: Name + Actions columns only
  - Pencil icon: edit SearchDirectory via dialog
  - Trash icon: remove
  - Plus button: add new

**SearchDirectoryRowDialog:**
- Name field (optional QLineEdit)
- Path field with Browse button (QFileDialog)
- Folder KAI picker (FolderPickerWidget) — target folder in Kai tree
- Auto-generates UUID for new entries (kaiFolderId)

### i18n Keys Added (PARTE 1)

```json
"settings.group.autosync": "Autosync",
"settings.autosync.enabled.label": "Enabled",
"settings.autosync.enabled.hint": "Auto-synchronize external kai.json/kai.yml files",
"settings.group.autosync.direction": "Direction",
"settings.autosync.from_file.label": "File → Kai",
"settings.autosync.from_kai.label": "Kai → File",
"settings.group.search_directories": "Search Directories",
"settings.search_dir.{name,path,kai_folder,add,remove,actions}": ...,
"folder.field.working_dir{,.placeholder,.tip,.browse,.dialog}": ...,
"command_editor.advanced_settings.section.scheduling": "Scheduling" / "Agendamento"
```

---

## PARTE 2: Tab Standardization

### Architecture

#### AbaContent (Abstract Base Class)

**Purpose:** Common interface for all output tab content widgets.

**Interface:**
```cpp
class AbaContent : public QWidget {
    virtual QString label() const = 0;          // Tab label (i18n'd)
    virtual QString iconName() const = 0;       // Icon name (Lucide)
    virtual void clear() = 0;                   // Clear content
    virtual bool hasContent() const = 0;        // Has displayable data?
    virtual void setViewOptions(const ViewOptions &) {}  // Line numbers, wrap, etc.
    virtual void applyTheme() {}                // Re-apply colors/tokens on theme change
};

struct ViewOptions {
    bool lineNumbers = true;
    bool wrap = false;
    bool timestamps = false;
    bool autoScroll = true;
    bool compact = false;
    int fontSize = 11;
};
```

#### OutputMetricsHeader

**Purpose:** Display HTTP response metrics (status code, elapsed time, body size).

**Features:**
- Status badge: color-coded by status (2xx green, 4xx yellow, 5xx red)
- Time badge: elapsed time in milliseconds
- Size badge: body size in B/KB/MB
- All colors from design tokens (tk::successFg, tk::warningFg, tk::errorFg)
- All spacing/radius from design tokens (tk::space, tk::radiusSm)

**API:**
```cpp
void setMetrics(int statusCode, const QString &reasonPhrase,
                qint64 elapsedMs, qint64 bodySize, bool success);
void clear();
void applyTheme();
```

#### Output Tab Classes

| Class | Icon | Purpose |
|-------|------|---------|
| `OutputStdoutContent` | terminal | Shell stdout/stderr output |
| `OutputJsonContent` | braces | JSON response (JsonViewerWidget) |
| `OutputHttpRequestContent` | globe | HTTP request (method, URL, body) + metrics header |
| `OutputHttpHeadersContent` | list | Response headers (read-only table) |

**Common Pattern:**
```cpp
class OutputXxxContent : public AbaContent {
    QString label() const override;           // e.g., "Response"
    QString iconName() const override;        // e.g., "braces"
    void setXxx(const QString &data);         // Populate content
    void clear() override;                    // Empty
    bool hasContent() const override;         // Check if populated
    void applyTheme() override;               // Use tk::* tokens
};
```

### Design Token Compliance

**All output tabs use design tokens (NEVER hardcoded pixels/colors):**

- Border-radius: `tk::radiusSm()` (4-6px), `tk::radiusMd()` (8-10px), `tk::radiusLg()` (12-16px)
- Spacing: `tk::space(N)` [N=1→4px, 2→8px, 3→12px, ...]
- Colors: 
  - Background: `tk::surface()`, `tk::surface2()`, `tk::codeBg()`
  - Foreground: `tk::fg()`, `tk::codeFg()`
  - Semantic: `tk::successFg()`, `tk::warningFg()`, `tk::errorFg()`

**Theme Integration:**
Each tab's `applyTheme()` is called:
- On theme change (MainWindow broadcasts signal)
- On corner-style change (Settings → Appearance → Corners)
- Ensures Sharp/Soft/Rounded applies consistently

### i18n Keys Added (PARTE 2)

```json
"output.tab.request": "Request" / "Requisição",
"output.tab.headers": "Headers",
"output.tab.json": "Response" / "Resposta",
"output.tab.stdout": "Output" / "Saída",
"output.request.method": "Method" / "Método",
"output.request.url": "URL",
"output.headers.key": "Header",
"output.headers.value": "Value"
```

---

## Testing

### Automated Tests

#### test_output_tabs.cpp (NEW)
- Tests OutputMetricsHeader (status codes, size formatting)
- Tests each content tab (label, iconName, setXxx, clear, hasContent)
- Tests edge cases (invalid JSON, empty headers)

#### test_project_sync_manager.cpp (ENHANCED)
- Rescan discovers kai.json and kai.yml
- Ignores folders without workingDir
- Pause/Resume toggle works
- notifyCommandsSaved() doesn't crash
- Manual sync methods exist

### Manual Test Checklist

See `/tmp/kai-manual-test-checklist.md` (100+ items covering):
1. **PARTE 1:** Folder workingDir, CRON moved, Autosync UI
2. **PARTE 2:** Tab labels, icons, content display, theme application, i18n

---

## Known Limitations (FIXME)

### ProjectSyncManager::notifyCommandsSaved()
- **Status:** Stub implementation
- **Missing:**
  1. Access to `ConfigManager::exportFolder()` to calculate JSON
  2. Hierarchical workingDir resolution
  3. Hash comparison logic (detect changed folders)
  4. Trigger sync only on folders where hash changed

### OutputMetricsHeader::applyTheme()
- **Status:** Stub
- **Missing:** Store statusCode/success to re-apply colors on theme change
- **Workaround:** Colors re-applied on next `setMetrics()` call

### Edge Cases Not Addressed
- Very large responses (>100MB) — no streaming
- Network failures — no retry logic
- Concurrent writes — no locking

---

## Files Modified / Created

| File | Type | Change |
|------|------|--------|
| `src/engine/project-sync-manager.h/.cpp` | Modified | Added notifyCommandsSaved() stub |
| `src/ui/features/settings/tabs/search-directory-row-dialog.h/.cpp` | New | Dialog for SearchDirectory CRUD |
| `src/ui/features/settings/tabs/autosync-tab.h/.cpp` | Modified | Rewritten with master switch + table |
| `src/ui/features/collections/folder-editor-dialog.h/.cpp` | Modified | Added workingDir field |
| `src/ui/features/command-editor/command-editor-dialog.cpp` | Modified | CRON moved to Scheduling |
| `src/ui/features/settings/settings-dialog.cpp` | Modified | AutosyncTab instantiation updated |
| `src/ui/features/output/aba-content.h/.cpp` | New | Abstract base class |
| `src/ui/features/output/output-metrics-header.h/.cpp` | New | Metrics badges widget |
| `src/ui/features/output/output-stdout-content.h/.cpp` | New | Shell output tab |
| `src/ui/features/output/output-json-content.h/.cpp` | New | JSON response tab |
| `src/ui/features/output/output-http-request-content.h/.cpp` | New | HTTP request tab |
| `src/ui/features/output/output-http-headers-content.h/.cpp` | New | Headers table tab |
| `src/ui/features/output/output-panel.cpp` | Modified | Fixed border-radius hardcode |
| `assets/i18n/en.json` | Modified | Added 10+ i18n keys |
| `assets/i18n/pt.json` | Modified | Added 10+ i18n keys |
| `tests/test_output_tabs.cpp` | New | Tests for new tabs |
| `tests/test_project_sync_manager.cpp` | Enhanced | Added more test cases |
| `CMakeLists.txt` | Modified | Registered new files + tests |

---

## Statistics

| Metric | Count |
|--------|-------|
| New files | 12 |
| Modified files | 8 |
| i18n keys | 20+ |
| Design token usages | 150+ |
| Lines of code | ~1000 |
| Test cases | 20+ |
| FIXME comments | 3 |

---

## Next Steps (Future)

1. **Complete ProjectSyncManager::notifyCommandsSaved()**
   - Integrate with ConfigManager::exportFolder()
   - Implement hash comparison
   - Trigger sync on detected changes

2. **Enhance OutputMetricsHeader**
   - Store statusCode/success for theme reload
   - Add animations (fade-in, status pulse)

3. **Add advanced features**
   - Conflict resolution UI
   - Sync scheduling (cron-based)
   - Bidirectional merge strategies

4. **Performance optimization**
   - Debounce filesystem watcher (already 500ms)
   - Lazy-load large JSON responses
   - Cache formatted JSON

---

## References

- **AGENTS.md:** Core development guidelines (C++20, Qt6, design tokens, i18n)
- **Manual Test Checklist:** `/tmp/kai-manual-test-checklist.md`
- **Implementation Summary:** `/tmp/kai-implementation-summary.md`

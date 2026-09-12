# Feature & improvement catalog

Ideas for evolving Kai, split into **new features** and **improvements to
existing routines**. Each item carries a *value* × *effort* note and a
short implementation sketch. Suggested prioritization at the end.

> **Status:** feature-frozen as of the current beta — nothing here lands
> without at least a minor version bump.

---

## A. New features

### A1. OS keyring for secret variables ⭐ (high value / medium effort)
Secrets are masked in the UI today but persist as plain text in
`settings.json`. Integrate **QtKeychain** (libsecret on Linux, Credential
Manager on Windows) to store the real value in the OS vault;
`settings.json` would only keep a reference.
- *Impl.*: QtKeychain as a FetchContent dependency; `EnvironmentManager`
  resolves the keyring value on demand for secret keys.

### A2. OpenAPI import in YAML (high value / medium effort)
The current parser only reads JSON. Add **YAML** support (most OpenAPI
specs are YAML).
- *Impl.*: bundle a light YAML parser (e.g. `rapidyaml`/`yaml-cpp`) and
  convert to `QJsonDocument` before reusing `parseOpenApi`.

### A3. Chained "send & extract" request flows
Build visual **flows** (request → extract → request) without manual
hooks, Postman-runner style.
- *Impl.*: a new "flow" type that sequences HTTP commands with an
  output→variable mapping between steps.

### A4. WebSocket / SSE (medium value / high effort)
Support persistent connections (WebSocket, Server-Sent Events) with live
logging in the output panel.

### A5. Saved response snippets / variables (medium value / low effort)
Let the user "pin" a value extracted from a run for manual reuse (e.g.
copy a returned `id` as a named variable).

### A6. Export a command as curl / code (medium value / low effort)
A button to **export** an HTTP command as `curl` (the reverse of import)
or as a code snippet (fetch/axios/requests).

### A7. Config sync/backup (medium value / medium effort)
Export/import the whole workspace (commands + environments + collections)
as a single file, or sync it via git/a cloud folder.

### A8. Assisted OAuth2 authentication (high value / high effort)
A built-in OAuth2 flow (client credentials / auth code), storing and
auto-refreshing the token.

### A9. Notifications (medium value / medium effort)
Notify the user about execution events without the window needing focus:
a native OS notification (tray/Notification Center) and/or an internal
toast. Use cases: a long-running command **finished** (success/failure)
while the window is hidden or minimized, a background process **died**,
or an auto-run completed. Ideally configurable per command (e.g. "notify
on finish") plus a global level (never / failures only / always),
respecting the "hide window on run" option.
- *Impl.*: native notification per platform (D-Bus
  `org.freedesktop.Notifications` or `QSystemTrayIcon::showMessage` on
  Linux; a native toast API or `QSystemTrayIcon` on Windows); fired from
  `ExecutionPipeline`'s `pipelineFinished`/`backgroundProcessStarted`
  signals; a per-command flag (`Command`) plus a global preference in
  `SettingsData`.

---

## B. Improvements to existing routines

### B1. `test_cli_client` robustness (low value / low effort) — tech debt
The test assumes **no** Kai instance is listening; if one is, the
`listWithoutInstanceReportsConnectionError` case fails (an observed flake
whenever a dev instance is left running).
- *Impl.*: use a test-specific socket name (e.g. a `KAI_IPC_SOCKET`
  environment variable) to isolate it from a real Kai instance.

### B2. Mask secrets in logs/history, not just the UI (high value / low effort)
A command's output can currently echo a token as plain text. When saving
to history (`runs.json`) and when showing it in the output panel,
**replace** the values of secret-marked variables with `***`.
- *Impl.*: `EnvironmentManager` exposes the secret values; a filter
  applies the mask to the text before logging/saving.

### B3. Headless UI tests for the newer screens (medium value / medium effort)
Environments, Runs, Help and the cURL import button don't have UI tests
yet. Add `QTest` cases that instantiate the dialogs offscreen and verify
their behavior (open, filter, select).

### B4. Persist the Help splitter position and last topic (low value / low effort)
Remember the splitter position and the last opened topic in Help v2.

### B5. More robust `windeployqt` in the cross-build (medium value / medium effort)
The Windows build via wine sometimes produces a folder missing a DLL.
Validate that the essential DLLs are present and fail early with a clear
message (today it's just a warning).

### B6. Fuzzy search highlighting in the command tree (medium value / low effort)
The search already filters; add a **highlight** of the matched substring
in the name (Help v2 could use the same treatment).

### B7. More visible active-environment indicator (low value / low effort)
Show the active environment's name in the theme's accent color and,
maybe, a badge when the package has secret variables.

### B8. Import a `.env` file into an environment (high value / low effort)
A button to import a `.env` file (`KEY=VALUE` per line) directly into an
environment package.

### B9. Inline JSON validation in the Body editor (low value / low effort)
Underline a JSON error in Body as you type (today it only validates on
format).

### B10. One-click duplicate for a command/folder (low value / low effort)
Duplication already exists in a few places; standardize it across the
whole tree via the context menu and a shortcut.

---

## Suggested order of attack

1. **B2** — mask secrets in logs/history (security, cheap).
2. **B8** — `.env` import (adoption, cheap).
3. **A1** — OS keyring (closes the secrets loop).
4. **A2** — OpenAPI YAML (real-world adoption).
5. **B1** — CLI test robustness (hygiene).
6. **A6** — export as curl/code (adoption).
7. **B3** — UI tests for the newer screens (quality).
8. **A3 / A8** — chained flows and OAuth2 (big features, plan them out).

> Living list: add to it or reorder it as real usage reveals actual pain
> points. Same principle as CopyQ — native, light, stays out of the way.

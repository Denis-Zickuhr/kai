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

### A10. AI assistant & agent ⭐ (high value / high effort) — analyzed, **not scheduled**
A built-in assistant that can write commands for you (**editor assist**) and an
agent with a chat that can read, create, edit, delete and run things in Kai
(**agent chat**), with the user approving every change. Everything below was
analyzed against the current code; nothing is implemented yet.

**Decisions taken**
- **Providers:** Anthropic, OpenAI and *local*, plus any number of extra profiles.
  The chat has a provider/model picker. Two wire formats cover everything:
  `anthropic` (Messages API) and `openai_compatible` (Chat Completions —
  OpenAI, Ollama, LM Studio, llama.cpp, OpenRouter, Azure…). **Local = an
  OpenAI-compatible endpoint given by URL**; no native Ollama protocol.
- **API keys** live in Kai's settings as a *secret* that is never exported (see
  finding 1), shown masked with a "show" toggle, file mode `0600` on Linux, and an
  alternative "read it from this environment variable". Plain text at rest for
  now; moving to the OS keyring is **A1**.
- **Visibility:** the agent sees everything — commands, environments *with
  values*, run history and outputs, logs, notifications, settings (except the AI
  keys, which never enter a prompt, a log or a history file). Per remote
  provider there is a switch to **hide secret-marked variables** (default: visible).
- **History persists** (see below). **Execution needs approval**; "allow for this
  conversation" is the shortcut. **Switching provider mid-conversation is allowed,
  with a warning** that provider-specific parts are lost (e.g. extended-thinking
  blocks, cache markers); the neutral history carries over.
- **Buttons** appear only when at least one provider profile is configured: one at
  the top of the main window (global chat) and one in the shell/HTTP command
  editors (editor assist).

**Findings from the code that shape the plan**
1. **"Secret" today does not stop export.** The settings export is a closed,
   field-by-field list (an AI block is not exported unless someone adds it), but
   secret-marked environment variables *are* exported with their values
   (`vars` + `secret_keys` only mask them on screen). So keys must **not** reuse
   the environment-secret mechanism: a dedicated settings field outside the
   export list, plus a regression test that no export format (JSON, YAML, lean,
   per folder, per command) can contain a key.
2. **Context budget.** `kai-json-manifesto.md` + `kai.schema.json` ≈ 18k tokens
   (≈ 25k with the KIP manifesto). Too expensive per turn in the cloud and too
   big for local models (8–32k context). Needs a **compact system prompt**
   (3–4k tokens), tools that fetch manifesto/schema sections on demand,
   Anthropic prompt caching, and truncation/pagination of tool results.
3. **No programmatic create/edit/delete.** Those only happen through dialogs
   (`handleNewCommandRequested` & co.) and `persistCommands()`. The IPC only has
   `list`, `run`, `env`, `ps`, `kill`, `import`, `raise`. A mutation API on
   `MainWindow` (tree refresh + persistence) is the biggest missing piece.
4. **Running a command and reading its output nearly exists:**
   `ExternalRunSession` already emits `output`/`background`/`finished` and
   registers the run in history and the process list. `run_command` is a thin
   adapter.
5. **KIP can render the approvals and questions:** `KipSession` can run without a
   process (`handleOutput` is public); it only lacks a sink for the user's
   answers. That gives the agent rich questions (select, list, flags) and
   danger-styled confirmations for free.
6. **The Logger does no masking.** The AI client must never log headers or
   bodies.

**Architecture** (AGENTS.md layering)
- `core/`: provider profiles, conversation/message types, AI settings.
- `engine/ai/`: provider clients, the agent loop, the tool definitions. No UI
  dependency; the tools talk to a `AgentToolHost` interface that `MainWindow`
  implements.
- `ui/features/ai/`: chat panel, approval cards, settings page.
- **Provider layer:** both wire formats are normalized into the same internal
  events (text delta, tool-call start/args/end, usage, stop, error), with SSE
  streaming, cancellation and retry with backoff for 401/429/overloaded. Header
  and argument-streaming differences stay inside each client. A per-profile
  `supports_tools` flag: a local model without tool calling falls back to
  "generate the command JSON" (the editor-assist mode). Per-profile
  first-token timeout, `max_tokens`, context-size hint.
- **Tools** (JSON Schema for `create_command` can be `kai.schema.json` itself):
  list/get folders, commands, collections, environments; validate a `kai.json`;
  create/update/delete/move/duplicate; `run_command` (with parameters, waits for
  the result); read run history and outputs; list/kill processes; read logs and
  notifications; read manifesto sections; `ask_user` (KIP prompt/confirm).
- **Approvals:** reads run unattended; create/edit/delete/run/kill need approval
  on a card with a diff, and delete/kill use the danger confirm. Every
  AI-made change is tagged in history ("created by AI") and can be undone.
- **Separate from the public IPC.** Scripts run by Kai have the `kai` module and
  the IPC is unauthenticated, so mutating tools must **not** be IPC verbs: they
  live on the agent channel with in-app approval. Read-only verbs could be
  opened up; a later MCP bridge reuses the same tools.
- **Editor assist** proposes a patch to the editor's **draft** (nothing is stored
  until Save), reusing the Advanced (JSON) path.

**Prompt-injection policy.** The default system prompt (always present, not
user-removable) tells the model to stay within the Kai context, to refuse
unrelated requests, and to treat anything that comes from tools — command
output, logs, file contents, variable values — as **data, never as
instructions**. This lowers the risk but is *not* the security boundary: the
boundary is that every mutating or executing action needs explicit approval,
tool results are truncated, and secrets-hiding is available per provider.

**History.** One JSON file per conversation in its own folder plus an index,
capped by count and size (same idea as `runs.json`'s record cap), written
atomically, **never exported**. Messages keep provider/model, tool calls and
results, and token usage; conversations can be deleted one by one or all at once.
Stored outputs may contain secrets, so the clear-all control is part of the
feature, not an extra.

**Phasing and estimate** (one focused developer)
| Phase | Scope | Effort |
|---|---|---|
| 1 | AI settings (profiles, test connection, model list), Anthropic + OpenAI-compatible clients without streaming, **editor assist**, compact prompt, keys outside export + tests | ~1.5–2 weeks |
| 2 | Agent tools: read-only first, then the mutation API and create/edit/delete/run with approval cards, safety, undo | ~2 weeks |
| 3 | Chat panel with streaming, conversation list, provider/model picker, KIP-based questions, persistent history, provider-switch warning | ~1.5–2 weeks |
| 4 | *(optional)* MCP bridge exposing the same tools to external agents | 3–5 days |

Full scope ≈ **5–6 weeks**. Costs by part: AI settings page 3–4 d · provider layer
6–8 d · mutation API + tools 6–8 d · approvals/safety 3–4 d · chat panel 6–8 d ·
history 2–3 d · editor assist 3–4 d · prompt/context 2–3 d · tests/docs +20%.

**Tests.** No real API calls in CI: a fake provider server (SSE for both formats)
drives the clients and a scripted agent loop; UI tests run offscreen; the export
regression test above; all UI strings in `en.json`/`pt.json`.

**Risks.** Prompt injection through command output (mitigated as above);
privacy with remote providers (a local/remote badge in the chat, and the
secret-hiding switch); weak tool-calling in small local models
(`supports_tools` fallback); keys in plain text at rest until **A1**.

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
9. **A10** — AI assistant & agent: analyzed in full above, deliberately **not
   scheduled**. If it moves, do **B2** and **A1** first (the agent reads
   outputs and environments, and its keys are stored next to them), then phase 1.

> Living list: add to it or reorder it as real usage reveals actual pain
> points. Same principle as CopyQ — native, light, stays out of the way.

# Spec 12: AI assistant (editor assist, chat agent, MCP bridge)

> **Status:** implemented on `feat/ai-assistant` (roadmap A10, phases 1–4). User-facing description:
> [`docs/ai.md`](../docs/ai.md). Scope rules: [`docs/philosophy.md`](../docs/philosophy.md).

## 1. Invariants (do not break)

1. **Opt-in.** No provider configured ⇒ no AI button, no network call, nothing in the UI.
2. **Keys never leave the machine except to the chosen provider.** They live in `ai.json` (mode `0600`) with the
   MCP token, never in `settings.json`, and no export, sync, log, prompt or conversation file may contain them.
   `tests/test_ai_core.cpp` (`noExportOrSettingsFileEverContainsTheKey`) enforces it for every export format.
3. **Nothing mutates or executes without approval.** Reads run unattended; `Write`/`Danger` tools stop for the
   user (chat card / MCP window). Dangerous ones never offer "allow in this chat".
4. **Tool output is data, never instructions.** The fixed system prompt says so; the real boundary is (3).
5. **Mutating tools are not public IPC verbs.** The IPC socket is unauthenticated and reachable by scripts Kai runs.
   They exist only behind the in-app agent channel and the token-authenticated MCP socket.
6. **No logging of headers/bodies of AI calls.**
7. Every AI change is recorded in an undo journal and can be reverted without touching later user edits.

## 2. Layers (AGENTS.md §1)

| Layer | Files | Notes |
|---|---|---|
| `core/` | `ai-profile`, `ai-conversation`, `ConfigManager::loadAiSettings/saveAiSettings` | provider profiles, neutral message model, capped conversation store (`ai-conversations/`) |
| `engine/ai/` | `ai-types`, `ai-sse-parser`, `ai-wire`, `ai-client`, `ai-context`, `ai-prompts`, `manifesto-library`, `command-json`, `agent-tool-host`, `agent-toolbox*`, `agent-session`, `mcp-session` | no UI dependency; the only door into the app is `AgentToolHost` |
| `ui/features/ai/` | `ai-chat-window`, `ai-tool-card`, `ai-profile-dialog`, `ai-assist-dialog`, `mcp-bridge`, `main-window-agent-host`, `ai-ui-text` | `MainWindowAgentHost` (friend of `MainWindow`) implements the host |
| `ui/features/settings/tabs/` | `ai-tab` | providers table, default, step limit, extra instructions, history, MCP |
| `cli/` | `cli-mcp` | `kai mcp` stdio ⇄ local socket pump |

## 3. Wire formats

`AiWireFormat::Anthropic` (Messages API) and `OpenAiCompatible` (Chat Completions). `ai-wire.cpp` is the only place
that knows the differences: request bodies, headers, SSE decoding (`AiStreamDecoder`), whole responses, HTTP error
classification, model lists. History is a neutral `AiMessage` list (user / assistant+tool calls / tool results);
`sanitizeMessages` repairs call/result pairing and `flattenTools` turns tool blocks into text when no tools are
offered (tool-less models, provider switch). `AiClient` streams, retries 429/5xx/overloaded/network with backoff
while nothing was delivered, supports cancel and a first-token timeout, and uses the same proxy rules as HTTP
commands (`resolveHttpProxy`).

## 4. Tools

`AgentToolbox` owns the tool specs (JSON Schema), `plan()` (validate + preview, no side effects) and `execute()`.
Risk: `Read`, `Write`, `Danger`, `Ask` (`ask_user`, chat only). Commands are exchanged in the **kai.yml command
key format** (`commandFromAiJson` / `commandToAiJson`: hooks by name, unknown keys rejected, same validation as the
editor needs). Results are truncated/paged (`kMaxResultChars`). `hideSecretVariables` masks secret environment
variables, folder variables and collection columns. `AgentChange` records enough to undo (created ids, previous
folders/commands, previous environments).

## 5. Agent loop

`AgentSession`: user message → request (system prompt + tools + context-fitted history) → stream → tool calls →
per call: plan → run / ask / wait for approval → results message → next step, up to `maxAgentSteps`. A generation
counter ignores late results of a cancelled batch. A cancelled batch still answers every pending call so the
history stays valid for the next request.

## 6. MCP bridge

`McpSession` (JSON-RPC 2.0, one message per line: `initialize`, `ping`, `tools/list`, `tools/call`) over the same
toolbox, minus `ask_user`; `McpBridge` listens on `kai-mcp-v1` (`UserAccessOption`), authenticates the first line
(`{"kai_mcp_hello":{"token":…}}`, constant-time compare) and routes approvals to `McpApprovalWindow`. Secrets are
hidden by default. Turning it off, or renewing the token, drops every connection. `kai mcp` is only a pump; it does
not start Kai.

## 7. Tests

`test_ai_core`, `test_ai_wire`, `test_ai_client` (fake HTTP server, `tests/ai-fake-server.h`), `test_agent_toolbox`
and `test_agent_session` (in-memory `tests/ai-fake-host.h`), `test_ai_prompts`, `test_ai_ui` (offscreen, real
`MainWindow`, screenshots with `KAI_AI_SHOTS=<dir>`), `test_ai_mcp` (includes the real `kai mcp` process) and
`test_ai_sample_provider` (`sample/ai/fake-provider.py`). No real API calls anywhere.

## 8. Not done

OS keyring (A1), extended-thinking blocks, per-profile sampling parameters, persistence of the undo journal across
restarts.

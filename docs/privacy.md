# Privacy

Kai has **no telemetry, no analytics, no accounts and no automatic update
check**. It does not contact any server of the project's author. It only makes
network requests that you configure or trigger.

## What can leave your machine

| Feature | Sent to | When |
|---|---|---|
| **HTTP commands** | The URL you wrote (through the proxy you set, or the system one) | When you run the command, or a schedule/hook you set up runs it |
| **AI assistant** (optional, off until you add a provider) | The provider you configured (Anthropic, OpenAI, Google, OpenRouter, a local Ollama/LM Studio, or any compatible address) | Only inside a conversation or editor request you started, or an agent you connected. **List models** and **Test connection** also call the provider, on click |
| **Your own commands** | Whatever they do (`curl`, `git`, a script…) | When you run them |
| **Links** | Your browser | When you click a link or turn on "open last link" for a command |

With a **remote** AI provider, what the assistant reads is sent to that host:
command text, variable values, outputs, logs. Turn on **Hide secret
variables from this provider** to replace secret values with `***hidden***`.
A local provider keeps everything on your machine.

## What stays on your machine

- Settings, commands, collections, run history and logs live in your config
  folder. Nothing is uploaded.
- AI API keys are kept in `ai.json` (mode `0600` on Linux), and are never
  exported, synced or shown to the assistant.
- Secret variables are never exported; only their names travel in project
  files.
- HTTP cookies live in memory only and are gone when Kai quits.
- The crash handler writes a stack trace to the local log. It does not send it.
- The CLI and the MCP bridge talk to the running Kai over a local socket,
  restricted to your user.

## How to turn it off

- **AI:** don't add a provider, or delete it in Settings → AI.
- **HTTP:** nothing is sent until you run an HTTP command.

Questions: open an issue at <https://github.com/Denis-Zickuhr/kai/issues>.

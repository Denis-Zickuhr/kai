# AI assistant

Kai has an optional **AI assistant**: an **editor assistant** that writes or changes a command for you, a **chat** that can read — and, with your approval, change and run — things in Kai, and an **MCP bridge** that lets an external agent use the same tools. It is **off until you add a provider**: with none configured there are no AI buttons anywhere.

> It follows the rest of Kai: nothing happens on its own. The assistant only acts inside a conversation you started, every change or run is shown to you first, and your keys never leave your machine except to the provider you chose.

## 1. Add a provider

**Settings → AI → Add.**

| Provider | Notes |
|---|---|
| **Anthropic** | API key from your Anthropic account. |
| **OpenAI** | API key from your OpenAI account. |
| **Local (Ollama)**, **Local (LM Studio)** | An OpenAI-compatible server on your machine or network. No key needed. |
| **OpenRouter**, **Other (OpenAI-compatible)** | Any server that speaks the Chat Completions API — type its address. |

Two wire formats cover everything: Anthropic's Messages API and the OpenAI-compatible Chat Completions API. "Local" is simply an OpenAI-compatible address — there is no native Ollama protocol.

Fields: **Name**, **Provider**, **Address** (only shown when it is not the provider's default), **Model** (type it, or press **List models** to ask the provider), **API key** (masked, with a *Show* button) or **a variable to read it from**, **Max answer tokens**, **Context size** (a hint; when set, Kai trims the oldest history to fit), **First-token timeout**, and two switches:

- **Supports tools** — turn it off for a local model without tool calling. The chat then only talks; the editor assistant still works (it asks for the command as JSON, no tools needed).
- **Hide secret variables from this provider** — values of variables marked secret are replaced by `***hidden***` in everything the assistant reads.

**Test connection** sends a tiny real request, so you know the key, address and model work before you save.

Other settings on the page: the **default provider**, the **max steps per message** (a safety limit on the agent loop), **extra instructions** (added *after* Kai's fixed rules, which can't be removed) and **Chat history** (how many conversations are saved, and *Delete all*).

### Where the key is kept

API keys live in `ai.json` in your config folder, with file mode `0600` on Linux — plain text at rest for now (an OS keyring is on the roadmap). They are **never** exported, synced, logged or put in a prompt: no export or sync format reads that file, and a test checks it. They are never part of `settings.json`.

## 2. Editor assistant

When at least one provider is configured, the command editor (form and JSON mode) shows an **AI** button next to *Advanced mode*. Describe what you want ("run the tests and stop on the first failure", "make it send the token in a header"), press **Generate**, and Kai shows a **diff** against the current command:

- `+` added, `-` removed, `~` changed (the old and new value).
- **Apply** puts the proposal back in the editor as a **draft** — nothing is saved until you press OK as usual.
- If the proposal is not valid (an unknown key, a duplicate name…) Kai sends the reason back to the model once and tries again.

The prompt carries the relevant sections of the Kai manifesto, so the model knows the real command format.

## 3. Chat

The **✨** button at the top right (or the **AI assistant** shortcut, unset by default — Settings → Shortcuts) opens the chat — a [panel](layout.md) docked on the right (move, float or close it like any other; it only exists while a provider is configured):

- a list of **conversations** on the left (saved, deletable one by one);
- the **provider picker** at the top, with a **Local / Remote** badge (remote means what the assistant reads is sent to that host) and a *Secrets hidden* badge when that is on;
- messages with Markdown (long lines and code wrap inside the window); each tool step is a one-line card you can open for the details; a **copy** button under each answer; a button to jump back to the latest message when you scrolled up (the view follows the answer only while you are at the bottom);
- a message box that grows with what you type, and a *Thinking…* line with a shimmer while the assistant works. The chat list hides itself on a narrow window (the button at the top left brings it back).

You can switch provider in the middle of a conversation. Kai warns first: the history carries over, but parts specific to the previous provider (thinking blocks, cache marks) are lost and the new provider reads the whole conversation.

### What the assistant can do

| Reads (no approval) | Changes (approval card with a preview) | Dangerous (confirmation) |
|---|---|---|
| folders, commands, collections, environments, notes, run history and outputs, processes, logs, notifications, a few settings, the Kai manifesto, `kai.yml` validation | create/edit/move/duplicate commands, create/edit folders, set an environment variable, create a note, **run a command**, **open a command in the Kai window** | delete a command, delete a folder, delete an environment variable, **stop a process** |

- Every change shows a card with the **diff** (or the command text to be run, with its parameters). **Allow**, **Deny**, or **Allow in this chat** (not offered for dangerous actions) — which stops asking for the ordinary changes until you press *Ask again*.
- Each change gets an **Undo** button on its card. Undoing only reverts what the assistant changed, not what you edited afterwards.
- `run_command` runs through Kai's normal pipeline — it shows in the process list, the history and the Output panel as if you had clicked it. Interactive-terminal and KIP commands are refused there (they need you at the keyboard): for those the assistant uses `run_in_app`, which — after your approval — **opens the command in the Kai window** as if you had clicked it (parameter form, interactive terminal or KIP screen) and you continue there. It does not see the output.
- The assistant can **ask you** a question with options when a decision is really yours.
- Anything that comes from a tool — command output, logs, file contents, variable values — is **data, never instructions**: the fixed system prompt says so, and the real boundary is that nothing changes or runs without your approval.

### Privacy

With a **remote** provider, what the assistant reads goes to that provider: command text, environment values, outputs, logs. Use **Hide secret variables** on that provider if you don't want secret values to be sent, or use a **local** one. Local providers show the *Local* badge.

### History

Each conversation is one JSON file in `ai-conversations/` plus an index, capped (100 conversations, 24 MB total — the oldest go first), written atomically, **never exported**. Stored outputs may contain secrets: **Settings → AI → Delete all** clears everything.

## 4. External agents (MCP)

**Settings → AI → External agents (MCP) → Allow external agents** lets tools that speak the [Model Context Protocol](https://modelcontextprotocol.io) (Claude Desktop, IDEs…) use Kai's tools. **Copy client config** gives you the snippet for their configuration:

```json
{ "mcpServers": { "kai": { "command": "/path/to/kai", "args": ["mcp"] } } }
```

- The client starts `kai mcp`, a small bridge that talks to **the running Kai** over a local socket authenticated by a token kept in `ai.json`. Kai must be open; `kai mcp` never opens it.
- The tools are the same as in the chat (without the question tool). Reads are answered; **every change or run opens a card in an *External agent* window** for you to Allow or Deny — there is no "allow always" for external agents. Recent changes are listed there with **Undo**.
- Secret variables are hidden from external agents by default (switch it in the same card).
- **Disconnect all agents** renews the token and drops every connection. Turning the option off closes the socket.
- The tools that change things are **not** verbs of the public CLI socket (which has no authentication, and scripts run by Kai can reach it).

## 5. Try it without a key

`sample/ai/fake-provider.py` is a tiny OpenAI-compatible server that answers with a script, tool calls included — see [`sample/ai/README.md`](../sample/ai/README.md).

## Limits and notes

- The chat needs a model with **tool calling** to act. Small local models that lack it work in chat-only mode and in the editor assistant.
- Long outputs are truncated and paged (`offset`/`limit`) before they reach the model; old tool results are shortened first when the context is tight.
- Kai never logs headers or request bodies of AI calls.

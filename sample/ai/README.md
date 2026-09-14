# AI sample — a fake provider

`fake-provider.py` is a tiny OpenAI-compatible server that pretends to be a model, so you can try the AI
chat (and the approval cards) **without a key or the internet**. It is not a language model: it looks for a
few keywords in your last message and answers with a scripted reply, tool calls included.

```bash
python3 sample/ai/fake-provider.py          # http://127.0.0.1:8765/v1
```

In Kai: **Settings → AI → Add**, provider **Local (Ollama)**, address `http://127.0.0.1:8765/v1`, model
`kai-demo`, no key. Then open the chat (the ✨ button at the top right) and try:

| You type | What happens |
|---|---|
| `list my commands` | reads your commands (no approval needed) |
| `create a folder` | an approval card with a preview; **Allow**, then **Undo** on the card |
| `delete the folder` | lists folders, then a **danger** confirmation to delete `Demo` |
| `run a command` | finds a command and asks to run it, then shows the output |
| `ask me a question` | a question card with options |

It also answers `GET /v1/models`, so **List models** and **Test connection** work.

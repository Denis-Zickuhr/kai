#!/usr/bin/env python3
"""A tiny OpenAI-compatible "model" for trying Kai's AI chat without a key or the internet.

It is NOT a language model: it reads the last user message, looks for a few keywords and answers with
a scripted reply — including tool calls, so you can see the approval cards, the diff preview, the
"allow in this chat" shortcut, the danger confirmation, ask_user and undo.

    python3 fake-provider.py            # listens on http://127.0.0.1:8765/v1
    python3 fake-provider.py --port 9000

In Kai: Settings -> AI -> Add -> "Local (Ollama)" preset, address http://127.0.0.1:8765/v1,
model "kai-demo", no key.
"""
import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MODEL = "kai-demo"


def last_user_text(messages):
    for m in reversed(messages):
        if m.get("role") == "user":
            c = m.get("content")
            return c if isinstance(c, str) else " ".join(p.get("text", "") for p in c or [])
    return ""


def tool_call(name, args, text=None):
    return {"text": text, "calls": [{"id": "call_%s_%d" % (name, int(time.time() * 1000) % 100000), "name": name, "arguments": args}]}


def parse_json(text):
    try:
        return json.loads(text)
    except Exception:
        return None


def decide(messages):
    """Return {"text": str|None, "calls": [...]} for the conversation so far."""
    last = messages[-1] if messages else {}
    user = last_user_text(messages).lower()

    # The previous step was a tool result: finish the turn, or chain the next step.
    if last.get("role") == "tool":
        content = last.get("content") or ""
        # Find which tool answered.
        name = None
        for m in reversed(messages):
            for c in m.get("tool_calls", []) if m.get("role") == "assistant" else []:
                if c.get("id") == last.get("tool_call_id"):
                    name = c["function"]["name"]
            if name:
                break
        if "denied" in content.lower():
            return {"text": "Okay, I will not do that.", "calls": []}
        if name == "list_folders" and ("delete" in user or "apag" in user):
            folders = parse_json(content) or []
            target = next((f for f in folders if f.get("name") == "Demo"), None)
            if not target:
                return {"text": "I did not find a folder called Demo to delete. Create one first (say: create a folder).", "calls": []}
            return tool_call("delete_folder", {"id": target["id"], "delete_contents": True}, "Deleting the Demo folder (this needs your confirmation). ")
        if name == "list_commands" and "run" in user:
            data = parse_json(content) or {}
            items = data.get("items") or []
            if not items:
                return {"text": "You have no commands to run.", "calls": []}
            return tool_call("run_command", {"id": items[0]["id"]}, "Running '%s'. " % items[0]["name"])
        if name == "list_commands":
            data = parse_json(content) or {}
            names = [i["name"] for i in data.get("items", [])]
            return {"text": "You have **%d command(s)**%s" % (data.get("total", 0), (": " + ", ".join(names[:10])) if names else "."), "calls": []}
        if name == "list_folders":
            folders = parse_json(content) or []
            return {"text": "You have %d folder(s): %s" % (len(folders), ", ".join(f["name"] for f in folders) or "none"), "calls": []}
        if name == "create_folder":
            return {"text": "Done — the folder is created. Use **Undo** on the card to revert it.", "calls": []}
        if name == "ask_user":
            return {"text": "You chose: %s" % content, "calls": []}
        if name == "run_command":
            return {"text": "The command finished. Output:\n\n```\n%s\n```" % (content[:400]), "calls": []}
        if name == "delete_folder":
            return {"text": "Deleted. Undo is available on the card.", "calls": []}
        return {"text": "Done.", "calls": []}

    if any(w in user for w in ("ask", "pergunt")):
        return tool_call("ask_user", {"question": "Which color do you prefer?", "options": ["Blue", "Green", "Red"]})
    if any(w in user for w in ("create", "cria", "criar")):
        return tool_call("create_folder", {"name": "Demo", "icon": "rocket"}, "I will create a folder called Demo. ")
    if any(w in user for w in ("delete", "apag", "remove")):
        return tool_call("list_folders", {}, "Let me look at your folders first. ")
    if "run" in user or "execut" in user:
        return tool_call("list_commands", {}, "Let me find your commands. ")
    if any(w in user for w in ("list", "comandos", "commands", "what", "quais")):
        return tool_call("list_commands", {}, "Let me check. ")
    if any(w in user for w in ("folder", "pasta")):
        return tool_call("list_folders", {})
    return {"text": "I am the **demo provider**: try *list my commands*, *create a folder*, *delete the folder*, *run a command* or *ask me a question*.", "calls": []}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[fake-provider] " + (fmt % args) + "\n")

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            self._json(200, {"object": "list", "data": [{"id": MODEL, "object": "model"}]})
        else:
            self._json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        req = json.loads(self.rfile.read(length) or b"{}")
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self._json(404, {"error": {"message": "not found"}})
            return
        reply = decide(req.get("messages", []))
        calls = reply["calls"] if req.get("tools") else []
        text = reply["text"]
        usage = {"prompt_tokens": 42, "completion_tokens": 7}
        if not req.get("stream"):
            message = {"role": "assistant", "content": text}
            if calls:
                message["tool_calls"] = [{"id": c["id"], "type": "function", "function": {"name": c["name"], "arguments": json.dumps(c["arguments"])}} for c in calls]
            self._json(200, {"choices": [{"message": message, "finish_reason": "tool_calls" if calls else "stop"}], "usage": usage})
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()

        def send(delta, finish=None, extra=None):
            chunk = {"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
            if extra:
                chunk.update(extra)
            self.wfile.write(("data: %s\n\n" % json.dumps(chunk)).encode())
            self.wfile.flush()

        for word in (text or "").split(" "):
            send({"content": word + " "})
            time.sleep(0.02)
        for i, c in enumerate(calls):
            send({"tool_calls": [{"index": i, "id": c["id"], "type": "function", "function": {"name": c["name"], "arguments": ""}}]})
            send({"tool_calls": [{"index": i, "function": {"arguments": json.dumps(c["arguments"])}}]})
        send({}, "tool_calls" if calls else "stop")
        self.wfile.write(("data: %s\n\n" % json.dumps({"choices": [], "usage": usage})).encode())
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("fake provider on http://%s:%d/v1 (model %s)" % (args.host, args.port, MODEL), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

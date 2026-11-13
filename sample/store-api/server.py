#!/usr/bin/env python3
"""Tiny Kai Store API for testing (standard library only).

  python3 server.py [--port 8765] [--token demo-token] [--no-auth]

GET /v1/store -> data/store.yml      GET /v1/index -> data/index.json      anything else -> files under data/
With a token, every request needs `Authorization: Bearer <token>`; otherwise the answer is 401 (so you can see Kai
report it). It prints each request with the token masked, to confirm the token only goes to this host.
"""
import argparse, os, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

def make_handler(token):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            auth = self.headers.get("Authorization", "")
            shown = (auth[:11] + "…") if auth else "-"
            sys.stderr.write("%s %s auth=%s\n" % (self.command, self.path, shown))

        def reply(self, code, body=b"", ctype="text/plain; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if token and self.headers.get("Authorization", "") != "Bearer " + token:
                return self.reply(401, b'{"error":"token required"}', "application/json")
            path = self.path.split("?", 1)[0]
            rel = {"/v1/store": "store.yml", "/v1/index": "index.json"}.get(path, path.lstrip("/"))
            full = os.path.realpath(os.path.join(DATA, rel))
            if not full.startswith(os.path.realpath(DATA) + os.sep) or not os.path.isfile(full):
                return self.reply(404, b"not found")
            with open(full, "rb") as f:
                self.reply(200, f.read())
    return Handler

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--token", default="demo-token")
    ap.add_argument("--no-auth", action="store_true")
    args = ap.parse_args()
    token = None if args.no_auth else args.token
    print("Kai Store API on http://127.0.0.1:%d  token=%s" % (args.port, token or "(none)"))
    ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(token)).serve_forever()

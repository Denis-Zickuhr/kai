# API store demo (token + signed index)

1. `python3 sample/store-api/server.py` (port 8765, token `demo-token`; `--no-auth` drops the token, `--token x` changes it).
2. Settings -> Store -> Add: **Name** `Corp (api)`, **Type** API, **Location** `http://127.0.0.1:8765`, **Token** `demo-token`.
   (Plain `http` is accepted only for localhost.)
3. **Test connection** shows "Corp private store · 3 packages · signed".

Things to try:

- Leave the token empty (or wrong): the test fails with "HTTP error 401".
- The server prints every request with the token masked; the token never goes to another host.
- `corp/needs-newer-kai` is greyed out. `corp/api-checks` creates the secret `API_TOKEN` empty.
- The index is signed (the public key is in `data/store.yml`). Change a character in `data/index.json` and the provider
  turns "invalid signature" and installs are blocked.

The files under `data/` use the same layout as a Git store (`packages/<publisher>/<slug>/<version>/`).

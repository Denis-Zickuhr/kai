# Importing from cURL

In the command editor, **HTTP** tab, click **Import cURL** and paste a
`curl` command. Kai recognizes the most common flags:

| Flag | Becomes |
|------|------|
| `-X` / `--request` | method |
| `-H` / `--header` | headers |
| `-d` / `--data*` / `--data-urlencode` | body (infers POST) |
| `--url` | URL |
| `-u` / `--user` | `Authorization: Basic ...` header |
| `-A` / `--user-agent`, `-e` / `--referer` | headers |

Supports `--flag=value`, single/double quotes, escapes and line
continuation (`\`). Irrelevant flags (`-s`, `-k`, `-L`, `--compressed`, ...)
are ignored without "eating" the URL.

## Example

```bash
curl -X POST https://api.x.com/login \
  -H 'Content-Type: application/json' \
  -H "Accept: application/json" \
  -d '{"user":"admin","pass":"123"}'
```

→ fills in the POST method, URL, two headers, and the JSON body.

# Importing from cURL

In the command editor, **HTTP** tab, click **Import cURL** and paste a
`curl` command. Kai recognizes the most common flags:

| Flag | Becomes |
|------|------|
| `-X` / `--request` | method |
| `-H` / `--header` | headers |
| `-d` / `--data*` / `--data-urlencode` | body (infers POST) |
| `--url` | URL |
| `-u` / `--user` | **Basic** authentication |
| `-A` / `--user-agent`, `-e` / `--referer`, `-b` / `--cookie` | headers |
| `-F` / `--form` | multipart form (`name=@path` uploads a file) |
| `-I` / `--head` | HEAD request |
| `-k` / `--insecure` | turns TLS verification off |
| `-x` / `--proxy`, `-m` / `--max-time` | proxy, timeout |

Supports `--flag=value`, single/double quotes, escapes and line
continuation (`\`). Irrelevant flags (`-s`, `--compressed`, `-o`, `--connect-timeout`, ...)
are ignored without "eating" the URL.

The reverse is in the HTTP page: **Copy as…** → cURL.

## Example

```bash
curl -X POST https://api.x.com/login \
  -H 'Content-Type: application/json' \
  -H "Accept: application/json" \
  -d '{"user":"admin","pass":"123"}'
```

→ fills in the POST method, URL, two headers, and the JSON body.

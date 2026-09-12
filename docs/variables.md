# Variables `{{VAR}}`

Shell, URL, Headers and Body all support `{{VAR}}` interpolation.

## Precedence

```
Global  <  Folder/Project  <  Dynamic (HTTP)  <  Parameters
```

The right side overrides the left. A missing variable becomes an empty
string (with a log warning) — Kai never breaks over it.

## Examples

```bash
echo "Port: {{PORT}}, Env: {{NODE_ENV}}"
```

```
GET {{BASE_URL}}/users/{{USER_ID}}
Authorization: Bearer {{AUTH_TOKEN}}
```

See [environments.md](environments.md) (where global variables live) and
[dynamic-vars.md](dynamic-vars.md) (variables generated on the fly).

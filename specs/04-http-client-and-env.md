# Spec 04: HTTP Client & Environment Injection/Extraction

## 1. HTTP engine
- Async execution via `QNetworkAccessManager`.
- `{{VAR}}` interpolation in the URL, headers and body.

## 1.1. Dynamic / faker variables
`{{$...}}` tokens are resolved at run time by
`EnvironmentManager::interpolate`:
- `{{$uuid}}` — UUID v4, no braces.
- `{{$timestamp}}` — epoch in seconds; `{{$timestampMs}}` in milliseconds.
- `{{$isoTimestamp}}` — UTC date/time in ISO-8601.
- `{{$randomInt}}` — integer in `[0, 100000)`; `{{$randomInt.N}}` in `[0, N)`.
- `{{$randomUuidHex}}` — 32 hex chars, no separators.
Unknown tokens fall back safely (empty string + a warning).

## 1.2. Template conditionals — `{% if %}`/`{% else %}`
Beyond plain `{{VAR}}` replacement, `EnvironmentManager::interpolate`
resolves conditional blocks **before** the `{{VAR}}` replace pass — useful
for "the user picked parameter A or B, decide which value/URL goes into
the final command":
```
{% if ENV_TARGET == "prod" %}https://api.prod.com{% else %}https://api.dev.com{% endif %}/users
```
- `{% else %}` is optional (a false condition without one becomes an empty
  string).
- **Operand:** a bare variable name (`ENV_TARGET`, no `{{ }}`), a
  `$dynamic` token, or a literal in single/double quotes (`"prod"`,
  `'prod'`).
- **Truthy** (no operator): `{% if DROP_DB %}` — true when the resolved
  value is non-empty and not `"false"`/`"0"`.
- **Comparison:** `<operand> OP <operand>`, `OP` in `== != > < >= <=`.
  Compares numerically when both sides parse as a number (a bare numeric
  literal like `1000`, or a variable whose value is numeric); string
  comparison otherwise (`==`/`!=`; an order operator on a non-numeric value
  falls back to `false` + a warning, never a crash).
- Nesting (`{% if %}` inside an `{% else %}`) works as an "elif".
- An `{% if %}` without a matching `{% endif %}` is malformed: the
  original text is preserved unparsed (safe fallback), never a crash.
- Content inside the chosen branch still goes through normal `{{VAR}}`
  replacement afterward.
- Scope: purely a template-TEXT decision — it doesn't control whether or
  how many times a command runs (that's what execution conditions are
  for, see spec 03 §3); implemented entirely inside `EnvironmentManager`,
  with no effect on command orchestration.

## 2. JSON → Env extraction (Env Extractors)
- On a `2xx` response, the JSON payload is parsed via `QJsonDocument`.
- The field at `json_path` (e.g. `data.token` or `auth.jwt`) is assigned to
  the variable configured in `env_var`, in the active Folder/Project or
  Global scope.

## 3. OpenAPI/Swagger import
- `core/openapi-parser` (`parseOpenApi`) reads an **OpenAPI 3.x / Swagger
  2.0 spec in JSON** (YAML not supported yet — convert to JSON first) and
  extracts its endpoints (`paths` × methods) as `HttpConfig` entries.
- Base URL: `servers[0].url` (OpenAPI 3) or `scheme://host+basePath`
  (Swagger 2).
- An example body is generated from the `requestBody`/schema (shallow
  recursion), with `Content-Type: application/json`.
- In the UI (File → Import OpenAPI/Swagger), Kai creates a **folder** named
  after the API's title and one **HTTP command per endpoint** (unique ids
  guaranteed).

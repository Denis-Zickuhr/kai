# HTTP client — design notes

Context: [docs/philosophy.md](../../docs/philosophy.md) says Kai must be a *complete but light* HTTP client. This
records the shape the implementation took, so later work stays in the same layers.

## Layers (each in one place)

```
HttpConfig / HttpDefaults        core/http-types.h, core/models.h     data + JSON, no behaviour
        │  (command + the folder chain, root first)
        ▼
resolveHttpRequest()             core/http-resolve.cpp                the inheritance rules, pure
        │  HttpRequestSpec (uninterpolated, merged, enabled rows only)
        ▼
materializeHttpRequest()         core/http-materialize.cpp            interpolation, auth, query, Content-Type, pure
        │  MaterializedHttpRequest (what goes on the wire)
        ├──────────────► exportHttpRequest()   core/http-export.cpp   cURL / Python / JavaScript
        ▼
HttpRunner                       engine/http-runner.cpp               Qt Network: send, redirects, TLS, proxy,
                                                                      cookies, streaming to a file, assertions
```

The pipeline (`ExecutionPipeline`) and the local CLI executor both call `core::folderChain` +
`resolveHttpRequest`; nothing else re-implements inheritance. The UI shows the same result
(`inheritedHttpHeaders`, `inheritedHttpAuth`).

## Rules worth remembering

- **Headers are an ordered list** of `{name, value, enabled}`. A level that mentions a name (case-insensitive)
  replaces every earlier row of that name; a disabled row removes the inherited header. The old `Name: value`
  map is still read.
- **Auth**: command → first folder going up that is not `inherit`; `none` cuts. A hand-written `Authorization`
  header always wins over the auth block.
- **Options** inherit field by field; defaults live in `HttpEffectiveOptions`.
- **Body type `raw` is the legacy behaviour**: no automatic `Content-Type`. A config without `body_type` loads as
  `raw`, so existing commands send exactly what they sent before.
- **Assertions reuse `ExecutionCondition`** and the existing condition engine; the response is exposed as
  `RESPONSE_*` built-in variables. With any assertion on they decide success (a 404 can be the goal).
- **Cookies** live in `engine::HttpCookieStore` (in memory, shared jar). Nothing is persisted.
- **Proxy `system`** reads the proxy environment variables only; the OS proxy is not consulted because it can block
  the GUI thread (PAC/WPAD).
- **Large responses**: with `response_file` the body streams to disk (2xx only) instead of being kept in memory.

## What travels in files

`http_defaults` (headers, auth, options) is a folder field: it goes in exports, in the folder sync (project root
via top-level `http_defaults`, subfolders inside their folder entry) and in `kai.yml` (top level and `folders[]`).
It is classified `Portable` in `core/field-scopes.cpp`. Secrets belong in variables (`{{API_TOKEN}}`): the validator
warns about a literal password/token/key.

## Not done (see docs/roadmap-ideas.md A11)

Custom CA / client certificates, WebSocket, responses in history, a cookie viewer, OAuth2 (A8),
OpenAPI in YAML (A2), a visual flow (A3).

## Checklist for a new HTTP field

model → `field-scopes` (if a new folder/command key) → `toJson`/`fromJson` → validator (`kai-file-validator.cpp`) →
manifesto block + JSON schema (`test_manifestos` fails otherwise) → `curl-parser` / `http-export` if it has a curl
equivalent → editor widget → i18n en **and** pt → `docs/commands-http.md` + `assets/help/*/commands_http.html` → tests.

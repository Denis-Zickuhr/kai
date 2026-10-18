# Philosophy: what Kai is, and what it is not

This is the reference for *deciding* things in Kai — what to build, what to
refuse, how to behave when a feature has two reasonable designs. When a
request conflicts with this document, say so and ask before building.

It is a living document. Items marked **(firm)** are decisions; items marked
**(open)** are directions that may change. Nothing here is carved in stone —
change the document when the project's mind changes, not the other way round.

---

## What Kai is

**A native, light command runner and HTTP client that lives in the tray and
stays out of the way.**

### 1. It stays out of the way **(firm)**

Kai is one global shortcut away and gone when you don't need it. The main
window **never opens by itself**: not at boot when "Start visible" is off, not
after a monitor or lid change, not because a background task finished. It
appears only on an explicit user action (global shortcut, tray click,
`kai show`, a command that asks for it). A window the user hid must stay hidden.

### 2. Native and light **(firm)**

C++20 and Qt Widgets. No Electron, no embedded browser, no runtime to
download. Startup, memory and idle cost matter more than any single feature.
A new dependency must justify itself against that. The GUI thread is never
blocked: file I/O, subprocesses and HTTP are always asynchronous.

### 3. A complete HTTP client **and** a command runner, in one place **(firm)**

Kai is not "a runner with a basic request type". HTTP is a first-class
citizen, meant to be a *complete* client (see
[What a complete HTTP client still needs](#what-a-complete-http-client-still-needs))
— but a **light** one. Completeness never means heavy: no accounts, no cloud,
no collaboration server, no telemetry. A request and a shell command share
the same variables, environments, hooks, conditions, schedules and CLI; that
sharing is the point.

### 4. It never leaves a mess behind **(firm)**

No orphaned processes. Background commands run in their own group and stop
for real; `cleanup` runs whether the command succeeded, failed or was
stopped. Quitting the app means everything it started is gone.

### 5. It fails safe **(firm)**

A missing variable becomes an empty string plus a log line, never a crash.
A corrupted settings or data file is recovered, never fatal. Bad input from
a user, a file or the network is an error message, not an exception.

### 6. Portable and versionable **(firm)**

What describes a *project* must be shareable and diffable: project, sync,
export and import files are **YAML only**, identified by names and paths
rather than internal ids. Local and personal things stay local: secrets never
leave the machine (only their names travel), collections are personal data,
global actions are links. "Portability above all; everything must be
versionable."

### 7. The user is in control: explicit and manual **(firm)**

Kai does what it was asked to do. Sync is manual (no watchers, no autosync).
It does not edit shell rc files, install anything or contact the network on
its own — anything like that is asked once, per case, with consent. A feature
that acts "helpfully" on its own needs an off switch and defaults to off.

### 8. From a one-liner to a small app **(firm)**

A command can start as a shell line and grow: Python/Node/PHP code, an HTTP
request, parameters and a form, hooks, conditions, schedules, auto-replies,
and finally a KIP command that renders its own UI. Growth is incremental —
the simple case stays simple.

### 9. One consistent, accessible design **(firm)**

One design system: theme tokens, density and corner-radius preferences are
honoured everywhere. Every visible string is internationalised (English and
Portuguese ship together). The keyboard is a first-class way to drive the app.

The window is a board of **panels** that can be moved, closed, floated and
restored (see [Panels & layout](layout.md)) — a flexible layout is a matter of
*where things are*, not a step towards an IDE. The default layout is the simple
one everybody sees until they touch it; floating windows never appear by
themselves; the layout is personal state (`layout.json`), never part of a
project; and "Restore layout" is always one click away.

### 10. Honest about quality **(firm)**

The tests are written with the change; a bug fix has a test that fails on the
old code. The build has no warnings. A change is reported as it is — failing
tests, skipped steps and untested paths are stated, not smoothed over.

---

## What Kai is not

- **Not an IDE or a code editor (firm).** It edits what it must (command
  bodies, documentation, notes) but does not compete with an editor: no
  project-wide refactoring, language servers or debuggers.
- **Not a heavy API platform (firm).** No accounts, no cloud workspace, no
  team server, no telemetry, no mandatory sign-in. A complete client yes; an
  online collaboration product no.
- **Not an automation service or daemon (firm).** It is a desktop app that
  runs while the user's session runs. It schedules and auto-runs things, but it
  is not a server, a CI system or a job queue.
- **Not self-acting (firm).** See principle 7. It does not open itself, sync
  itself, install itself or phone home.
- **Not locked to one OS (open).** Linux and Windows are developed and tested.
  macOS has no build only because nobody has a Mac to test on — it is not a
  rejection. Don't write code that makes a port harder, and don't promise one.
- **AI is an opt-in assistant, not the product (open).** Kai has an editor
  assistant, a chat that can read — and, with approval, change and run — things
  in Kai, and an MCP bridge for external agents (see [AI assistant](ai.md)).
  The rules that keep it light and safe: it does not exist until the user adds a
  provider (no buttons, no network); it never acts on its own (only inside a
  conversation the user started or an agent the user connected); every change or
  run is shown and approved, with undo; keys stay local and are **never**
  exported or synced; tool output is data and never instructions; secret
  variables can be hidden per provider; local providers are first-class.

---

## What a complete HTTP client needs — and where Kai stands

Status marks: **done** (in the app, with tests), **partly** (the useful core is in, the rest is named),
**open** (not started; see the [roadmap](roadmap-ideas.md)). The how-to is in [HTTP commands](commands-http.md).

### Must-have to be called "complete"

1. **Query parameters as a table** — **done** (rows with an on/off switch).
2. **Body types** — **done**: raw, JSON, XML, urlencoded form, multipart with file uploads, binary file, GraphQL.
3. **Authentication helpers** — **partly**: Basic, Bearer and API key (header or query), inheritable from folders.
   OAuth2 is open (roadmap A8).
4. **More methods** — **done**: HEAD, OPTIONS and any typed verb; a body is sent with any verb.
5. **Connection options per request** — **partly**: redirects (follow/limit), TLS verification on/off, HTTP/SOCKS proxy
   (from the environment variables or a URL), timeout — each inheritable. Custom CA and client certificates are open.
6. **Cookies** — **done**: an in-memory jar shared across requests, per-request opt-out, *Clear HTTP cookies*. It is not
   viewable yet and is never persisted (on purpose).
7. **Response handling** — **partly**: XML indented, binary and image content summarised/previewed, response saved to a
   file as it arrives, redirect chain and time to first byte. HTML rendering is open; per-phase timing is out of reach
   without a heavier HTTP stack.
8. **Export a request** — **done**: cURL, Python (requests), JavaScript (fetch) — the counterpart of the cURL import.

### Strongly expected

9. **Response assertions** — **done**: the same format as execution conditions, with `RESPONSE_*` variables; they decide
   success and stay available to the next steps.
10. **Chained flows** — **partly**: extractors + hooks + assertions already chain requests; a visual flow is open (A3).
11. **Request history with responses** — **open**: the run history keeps runs, not their responses (a response can hold
    secrets; this needs the masking work in B2 first).
12. **Other API styles** — **partly**: GraphQL is a body type and Server-Sent Events stream into the output (Stop ends
    them). WebSocket is open (A4) — it needs the extra `Qt6::WebSockets` module; gRPC is not planned.
13. **Imports** — **partly**: cURL, OpenAPI (JSON), Postman collections, Insomnia exports, HAR files and `.env` files.
    OpenAPI in YAML is open (A2).
14. **Secrets handling** — **partly**: secret variables are never exported and the validator warns about a literal secret
    in an auth block. Masking in logs/history (B2) and the OS keyring (A1) are open.

### Nice to have (all open)

15. Mock responses / a tiny local mock server; request cloning with overrides.
16. Pre-request and post-response scripting beyond hooks (kept deliberately small, using the languages Kai already runs).
17. Load/repeat a request N times or on an interval and summarise the results.
18. Certificate and JWT inspection helpers in the response view.

### Guardrails for all of the above

- Each item must keep Kai **light**: no new heavyweight dependency unless it
  is the only sane way (for example, a proper TLS or HTTP/2 stack is Qt's job).
- Anything that stores credentials follows principle 6: they never export.
- Anything that reaches out on its own (update checks, schema fetching,
  telemetry) is off by default and asked for explicitly.
- The request must stay expressible in the YAML project format, so it can be
  versioned and reviewed.

---

## How to use this document

- **Before building a feature,** check it against "What Kai is" and "What Kai
  is not". If it clearly fits, build it. If it conflicts, raise it first.
- **When two designs are both reasonable,** prefer the one that is lighter,
  more explicit, more portable and less surprising.
- **When the philosophy itself needs to change,** edit this file in the same
  change, and say why.

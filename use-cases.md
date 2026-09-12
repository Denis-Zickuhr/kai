# Spec: Use Cases & Acceptance Criteria

This document defines Kai's real-world use cases, for agent validation and
end-to-end acceptance testing.

---

## UC01: Environment variable hierarchy resolution
**Goal:** verify that variable precedence works correctly
(`Global < Folder/Project < Dynamic < Parameters`).

### Test scenario:
- **Global env:** `PORT=3000`, `NODE_ENV=production`
- **Folder/project env ("E-Commerce"):** `PORT=8080`
- **Command ("Run App"):** `echo "Port: $PORT, Env: $NODE_ENV"`

### Acceptance criteria:
1. Running "Run App" inside the "E-Commerce" scope, the output must be
   exactly: `Port: 8080, Env: production`.
2. The project variable (`PORT=8080`) must have overridden the global one
   (`PORT=3000`).

---

## UC02: Async shell command with real-time logging
**Goal:** validate that the UI never freezes during a long subprocess.

### Test scenario:
- **Command:** `for i in {1..3}; do echo "Step $i"; sleep 1; done`

### Acceptance criteria:
1. The output panel shows `Step 1`, `Step 2` and `Step 3` progressively,
   one second apart.
2. Kai's UI stays responsive (window can be moved, the search bar can be
   focused) during the 3 seconds of execution.

---

## UC03: Pre-hook pipeline with an HTTP client and JSON extraction
**Goal:** simulate authenticating against an API (obtaining a JWT) and
injecting it into the main command.

### Test scenario:
1. **Pre-hook command (HTTP):**
   - Method: `POST`
   - URL: `http://localhost:8080/api/v1/auth/login`
   - Simulated response payload: `{"data": {"token": "secret_jwt_xyz123"}}`
   - Extractor: `data.token` → `AUTH_TOKEN`
2. **Main command (Shell):**
   - Command: `echo "Bearer Token: {{AUTH_TOKEN}}"`

### Acceptance criteria:
1. The pre-hook runs first, extracts `secret_jwt_xyz123` and assigns it to
   `AUTH_TOKEN`.
2. The main command prints: `Bearer Token: secret_jwt_xyz123`.
3. If the pre-hook fails (e.g. HTTP 401), the main command must **not**
   run.

---

## UC04: Importing a project via `kai.json`
**Goal:** test the project selector and automatic scope creation.

### Test scenario:
- A `kai.json` at `/tmp/my-api/kai.json`:
```json
{
  "project_name": "My Local API",
  "env_vars": { "API_KEY": "12345" },
  "commands": [
    { "name": "Start Server", "type": "shell", "command": "npm start" }
  ]
}
```

### Acceptance criteria:
1. Picking the `/tmp/my-api` folder in the project selector imports it as
   "My Local API".
2. The command tree shows the "Start Server" command.
3. `API_KEY=12345` must be scoped exclusively to this folder.

---

## UC05: Persistence and recovery from a corrupted config file

**Goal:** verify `commands.json` resilience.

### Test scenario:

1. Write invalid content to `~/.config/kai/commands.json` (e.g.
   `{"folders": [invalid...}`).
2. Start Kai.

### Acceptance criteria:

1. Kai detects the corrupted JSON, creates
   `commands.json.bak.[TIMESTAMP]`, and starts up with a clean config
   without crashing.

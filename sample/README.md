# Sample — Kai's test environment

Each subfolder here is a local project with a `kai.json` at its root, so
you can try out the project selector, parameter types, collections and
per-folder variable resolution by hand — without touching your real
config.

## Available projects

- **`sample/api`** — "Kai API Tester": HTTP commands (GET/POST), response
  variable extraction (`env_extractors`), and a Collection (`Endpoints`)
  used as the source of a `select` parameter.
- **`sample/links`** — "Kai Web Shortcuts": shell commands that open URLs
  in the default browser (background), a parameter fed by a Collection
  (`Servers`), and an "app mode" example (Chromium/Chrome/Edge).
- **`sample/shell`** — "Kai Shell & Controls Demo": a showcase of the 4
  parameter types (text, select, bool, file), dynamic variables (`$uuid`,
  `$timestamp`...), a masked secret variable, a custom `working_dir`, a
  background process, and a Collection (`Customers`).

## Usage

1. Open Kai.
2. In the project selector, pick one of the folders above (e.g.
   `sample/shell`).
3. The project is imported using the `project_name` from its `kai.json`
   (e.g. "Kai Shell & Controls Demo").
4. Run the numbered commands in order — each one demonstrates a different
   Kai feature in isolation.

## Checking the variable hierarchy without the UI

The same scenario is covered by `tests/test_environment_manager.cpp`:

- Global: `PORT=3000`, `NODE_ENV=production`
- Folder "E-Commerce": `PORT=8080`
- Expected result: `Port: 8080, Env: production`

The folder only overrides the global values for the keys it defines —
`NODE_ENV` still comes from the global scope.

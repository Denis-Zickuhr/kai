# Sample — Kai's test environment

Each subfolder here is a local project with a `kai.yml` at its root, so
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
- **`sample/languages`** — "Kai Languages Demo": commands in Python and Node
  (the text is code, no `python3 -c` quoting), `{{VAR}}` vs. environment
  variables, a parameter read through `os.environ`, a free stdin, top-level
  `await` in Node and a per-command interpreter. Needs `python3`/`node`.
  Command 7 uses the injected **`kai` module** from Node; command 8 is
  **`showcase.py`**, a long interactive Python program (menu you keep returning
  to, a 4-step wizard with Back/dependent fields/chips/retry, environment and
  process tools, notifications, run-another-command) that uses both the `kip`
  and `kai` modules — see the docstring at the top of the file.
- **`sample/kip`** — "Kai KIP Demos": programs that speak the Kai Interface
  Protocol in plain bash (no helper): wizard, confirm, cascading fields, checklist,
  table, login with `set-env`, a hand-written raw-JSON example and a
  "command doesn't support KIP" case, plus a Python and a Node wizard written
  directly in the command with Kai's injected `kip` module (and a PHP one with the
  class `Kip`), plus a plain `.py` file that finds the module on disk. See its own README.

## Usage

1. Open Kai.
2. In the project selector, pick one of the folders above (e.g.
   `sample/shell`).
3. The project is imported using the `project_name` from its `kai.yml`
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

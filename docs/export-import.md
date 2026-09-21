# Export & Import

Move your setup between machines, share it with a team or keep it in git. Both live in the **File** menu.

## Export (File → Export…)

- **What to export:** everything, *a specific folder* or *a specific command*.
- **Include:** folders and commands, collections (name, folder, schema), **collection data** (off by default — a record can hold real data such as a token, so only the structure is exported unless you opt in), environments (variable packs), settings (appearance, shortcuts, window) and execution profiles. When you export a folder or command that uses collections, Kai asks which linked collections to bring along.
- **File format:** JSON or YAML.
- **Lean file** (recommended for versioning by hand): folders by name path, hooks by command name, no ids and no keys that equal the default.

> **Tip:** Environments can contain secrets. Review the file before sharing it, or leave environments out.

## Import (File → Import…)

Two sources, detected automatically:

- **File…** — a Kai export (JSON or YAML) or an [OpenAPI/Swagger](import-openapi.md) spec. For a Kai export you choose which categories to bring in. Folders and commands are *added* to what you have; an execution profile identical to an existing one is not duplicated, and one with the same name but a different configuration is renamed *(imported)*.
- **Project folder…** — a folder with a [kai.json](kai-json.md)/`kai.yml`, or any recognizable project (`package.json`, `docker-compose.yml`, `requirements.txt`/`pyproject.toml`, a Makefile…) when **Detect generic definitions** is on. You choose the parent folder and how `PROJECT_PATH` is written (useful on WSL, where the folder picker may return a Windows path).

To bring in a pasted `curl` command use the HTTP command editor — see [cURL Import](import-curl.md). From the terminal: `kai import ` and `kai validate ` (checks a kai.json/kai.yml for typos before importing) — see [CLI](cli.md).

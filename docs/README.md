# Kai documentation

A per-topic guide with ready-made examples. These documents mirror the
in-app **Help v2** (menu **Help**, with fuzzy search and topic navigation).

## Index

The order below is the order of the in-app **Help** (menu **Help**, which has a fuzzy search): from the first contact to advanced use.

**Start here**

| Topic | File |
|--------|---------|
| Overview | [overview.md](overview.md) |
| Folders, tree & search | [organizing.md](organizing.md) |

**Commands**

| Topic | File |
|--------|---------|
| Commands (shell) | [commands.md](commands.md) |
| Languages: Python, Node, PHP (and the built-in `kai` / `kip` modules) | [languages.md](languages.md) |
| HTTP commands | [commands-http.md](commands-http.md) |
| Parameters | [parameters.md](parameters.md) |
| Collections | [collections.md](collections.md) |
| Auto-replies | [responders.md](responders.md) |
| Hooks & execution conditions | [hooks.md](hooks.md) |
| Schedules & auto-run | [scheduling.md](scheduling.md) |

**Variables & environments**

| Topic | File |
|--------|---------|
| Variables `{{VAR}}` | [variables.md](variables.md) |
| Dynamic / faker variables | [dynamic-vars.md](dynamic-vars.md) |
| Environments (packages) | [environments.md](environments.md) |

**Interfaces & output**

| Topic | File |
|--------|---------|
| KIP — app-like command interfaces | [kip.md](kip.md) |
| The Output panel | [output.md](output.md) |

**Monitoring**

| Topic | File |
|--------|---------|
| Processes & PID | [cli.md](cli.md) |
| Run history | [runs.md](runs.md) |
| Notifications & logs | [notifications.md](notifications.md) |

**Projects & sharing**

| Topic | File |
|--------|---------|
| The `kai.yml` file | [kai-yml.md](kai-yml.md) |
| cURL import | [import-curl.md](import-curl.md) |
| OpenAPI/Swagger import | [import-openapi.md](import-openapi.md) |
| Export & Import | [export-import.md](export-import.md) |

**Terminal & CLI**

| Topic | File |
|--------|---------|
| CLI (command line) | [cli.md](cli.md) |
| Terminal targets & WSL | [terminal-targets-wsl.md](terminal-targets-wsl.md) |

**Customize**

| Topic | File |
|--------|---------|
| Settings | [settings.md](settings.md) |
| Keyboard shortcuts | [shortcuts.md](shortcuts.md) |
| Themes | [themes.md](themes.md) |

## Manifesto

- [KIP manifesto](../assets/manifesto/kip-manifesto.md) — the same idea for
  KIP: a self-contained guide that teaches an AI model (or a human) to write a
  program that talks the Kai Interface Protocol. Both manifestos can be copied
  with one click from **Help → Creation manifesto** inside Kai.
- [Kai manifesto (`kai.yml`)](../assets/manifesto/kai-manifesto.md) — a
  self-contained, YAML-only reference meant to feed an AI model (or a human) that
  needs to write a `kai.yml` for a project: the YAML rules Kai's parser follows, the
  complete model of every structure (command, parameter, HTTP, collection, hooks,
  conditions, responders), and complete example files.

## Roadmap / backlog

- [Feature and improvement catalog](roadmap-ideas.md)

> Technical specs live in [`../specs/`](../specs/).

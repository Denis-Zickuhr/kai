# The `kai.json` file (per project)

Place a `kai.json` at the root of a project and import it via **File →
Import Project**. Kai creates a folder with its commands and variables.

## Minimal format

```json
{
  "project_name": "My Project",
  "icon": "shopping-cart",
  "env_vars": { "PORT": "8080", "NODE_ENV": "development" },
  "commands": [
    { "name": "Dev Server", "type": "command", "command": "npm run dev", "is_background": true },
    { "name": "Migrations", "type": "command", "command": "npm run db:migrate" }
  ]
}
```

## Python and Node commands

`language` makes the text of a command **code** instead of a shell line (`"native"` is the default).
`interpreter` is optional and overrides the global one (Settings → Languages) for this command:

```json
{ "name": "Report", "type": "command", "language": "python",
  "command": "import os\nprint('env =', os.environ['NODE_ENV'])" },
{ "name": "Hello", "type": "command", "language": "node", "interpreter": "~/.nvm/versions/node/v22/bin/node",
  "command": "console.log('hello', process.env.USER)" }
```

`{{VAR}}` is not replaced in Python/Node code: variables and parameters are environment variables. See
[commands.md](commands.md).

## Versioned collections + a `select` parameter

```json
{
  "commands": [{
    "name": "Greet", "type": "command",
    "command": "echo {{customer.value}}",
    "params": [{
      "name": "customer", "type": "select",
      "collection": "Customers", "collection_display_field": "value"
    }]
  }],
  "collections": [{
    "name": "Customers",
    "schema": [
      { "name": "key", "label": "ID", "type": "key" },
      { "name": "value", "label": "Name", "type": "value" }
    ],
    "entries": [
      { "values": { "key": "1", "value": "Alice" }, "favorite": true }
    ]
  }]
}
```

The parameter references the collection **by name** (`collection`); Kai
resolves it to the id it generates on import.

## No `kai.json` yet?

Turn on **"Detect generic definitions"** in the Import Project dialog —
Kai recognizes `package.json`, `docker-compose.yml`,
`requirements.txt`/`pyproject.toml`/`manage.py`, `composer.json` and a
`Makefile`, and suggests ready-made commands from them, each ecosystem in
its own subfolder.

## Full reference

See the [`kai.json` manifesto](../assets/manifesto/kai-json-manifesto.md) for every
accepted field (hooks, execution conditions, auto-responders, parameter
types, and more) and a checklist for generating one with an AI model.

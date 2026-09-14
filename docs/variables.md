# Variables {{VAR}}

`{{VAR}}` is replaced right before a command runs. It works in **native commands**, the working directory, HTTP URL / headers / body, the response of an [auto-reply](responders.md) and the values of [execution conditions](hooks.md). In [Python/Node code](languages.md) it is *not* replaced — read environment variables instead.

Names accept letters, digits, `_` and `.` (the dot reaches a field of a [collection](collections.md) entry: `{{customer.email}}`); a `$` prefix is a [dynamic variable](dynamic-vars.md) (`{{$uuid}}`). Typing `{{` in an editor suggests the variables available.

## Precedence

```
Global  <  Folder/Project  <  Dynamic (HTTP)  <  Parameters
```

The rightmost overrides the leftmost. A missing variable becomes an empty string (with a warning in the log) — Kai never crashes because of it.

## Conditional text

A block can choose what text survives, before the variables are replaced:

```
docker compose up {% if {{BUILD}} == "true" %}--build{% else %}--no-build{% endif %} {{service}}
```

- `{% if A op B %}` … optional `{% else %}` … `{% endif %}`; blocks can be nested.
- Operators: `==`, `!=`, `>`, `=`, ` 1000 %}`); the order operators need numbers, otherwise the condition is false.
- Operands are `{{VAR}}`, quoted text or a number. With **no operator** the condition is true when the value is not empty, not `false` and not `0`: `{% if {{VERBOSE}} %}-v{% endif %}`.
- `kai --dry-run` shows the text after the blocks were resolved.

See [Environments](environments.md) and [Parameters](parameters.md).

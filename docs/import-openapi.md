# Importing OpenAPI/Swagger

Menu **File → Import OpenAPI/Swagger**. Pick a **JSON** file (OpenAPI 3.x
or Swagger 2.0).

Kai creates a **folder** named after the API's title (`info.title`) and
one **HTTP command per endpoint** (`paths` × methods):

- **Base URL**: `servers[0].url` (OpenAPI 3) or `scheme://host + basePath`
  (Swagger 2).
- **Example body**: generated from the `requestBody`/schema, with
  `Content-Type: application/json`.
- **Name**: `summary` > `operationId` > `METHOD /path`.

## Example

A spec with `POST /users` and schema `{ name, age }` becomes a `Create
user` command with URL `https://.../users`, POST method, and body:

```json
{ "name": "", "age": 0 }
```

> **YAML isn't supported yet** — convert the spec to JSON before
> importing.

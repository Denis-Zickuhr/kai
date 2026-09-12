# HTTP commands

Makes a request and shows the formatted response (navigable JSON).

## Fields

| Field | Description |
|-------|-----------|
| Method | GET / POST / PUT / PATCH / DELETE |
| URL | accepts variables: `{{BASE_URL}}/users` |
| Headers | key/value pairs (interpolated) |
| Body | JSON (the *Format JSON* button pretty-prints it) |

## Extracting a value into a variable (Env Extractor)

Pull a field out of the response's JSON into a dynamic variable, used by
the next commands:

```
data.token  →  AUTH_TOKEN
```

Then: `Authorization: Bearer {{AUTH_TOKEN}}` in a following command.

## Importing from cURL

The **Import cURL** button pastes a `curl` command and fills in
method/URL/headers/body. See [import-curl.md](import-curl.md).

## Response

The JSON response shows up **nested** in the output, as a collapsible
tree, with **Copy** and **Pop out** buttons (opens in its own window,
following the active theme). A separate **Request** tab shows exactly
what was sent (method, URL, headers, body) — useful once the request has
already run.

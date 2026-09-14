# HTTP commands

An **HTTP** command makes a request and shows the formatted response. In the command editor it has these pages:

| Page | What it holds |
|------|---------------|
| **General** | Method, URL, the **body** and its type |
| **Request** | Headers inherited from folders, the command's own headers, query parameters, authentication |
| **Connection** | Redirects, TLS, proxy, timeout, cookies |
| **Response** | Value extractors, assertions, save-to-file |

## Method and URL

GET, POST, PUT, PATCH, DELETE, QUERY, HEAD and OPTIONS are in the list; the field is editable, so any other verb (`PROPFIND`, `PURGE`…) can be typed. The URL accepts variables: `{{BASE_URL}}/users`.

## Headers, query parameters and authentication

Headers and query parameters are tables of rows. Each row has a checkbox to **turn it off without deleting it**. Query parameters are added to the URL when the request is sent (values are percent-encoded for you).

**Authentication** is a choice instead of a hand-written header:

| Type | Sends |
|------|-------|
| Basic | `Authorization: Basic …` from user and password |
| Bearer token | `Authorization: Bearer …` |
| API key | a header or a query parameter with the name and value you give |
| No authentication | nothing — and stops a folder's authentication from applying |
| Inherit | whatever the folders above define (default) |

Every value accepts variables. For secrets, use a variable marked **Secret** in the environment (`{{API_TOKEN}}`) — never type the token itself, or it ends up in exports and in git. An `Authorization` header you write by hand always wins over the authentication block.

## Share headers with a folder

Open a folder → **HTTP** tab. Headers, authentication and connection options set there are **inherited by every HTTP command in the folder and its subfolders**, so you stop repeating `Accept`, `X-Client` or the `Authorization` token in each request.

- A header with the same name (case-insensitive) set closer to the command replaces the inherited one.
- In a command's **Request** page the inherited headers appear in their own list with the folder they come from. **Untick one to turn it off for that command only.**
- Authentication is the first one found going up the folders; "No authentication" in a command or subfolder cuts the inheritance. Each connection option is inherited on its own.
- In a project's `kai.yml` the same settings go in `http_defaults` — see the [kai.yml guide](kai-yml.md).

## Body

The **body type** is chosen in the General page:

| Type | Use |
|------|-----|
| None | no body |
| Raw text | the text exactly as typed; no `Content-Type` is added (the original behaviour) |
| JSON / XML | the text, with `Content-Type: application/json` / `application/xml` unless you set one. JSON gets a validity check and the *Format* button |
| Form (urlencoded) | rows `name = value`, sent as `application/x-www-form-urlencoded` |
| Multipart form | rows `name = value`; tick **File** on a row to upload the file at that path |
| File (binary) | the content of one file is the body |
| GraphQL | the query, plus the variables as a JSON object; sent as `{"query": …, "variables": …}` |

## Connection

Every option can stay on **Inherit** (the folder's value, then Kai's default).

| Option | Default | Notes |
|--------|---------|-------|
| Follow redirects / max redirects | yes / 10 | the **Request** tab lists the chain that was followed |
| Verify TLS certificate | yes | turning it off is logged as a warning |
| Proxy | system | `system` reads `HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY` and `NO_PROXY` from the environment (the operating system's proxy settings are not consulted); `none`, or `http://host:port` / `socks5://host:port` |
| Timeout | off | **Enable** and set the seconds for this request or folder only; with it off a request waits as long as the server takes (useful while you debug the server). While waiting, the output shows **Waiting for response** with the elapsed time and **No timeout** (or the time left) |
| Cookie jar | on | cookies are kept **in memory while Kai is open** and shared by all requests; never written to disk. **File → Clear HTTP cookies** empties the jar |

## Response

The JSON response is a collapsible tree with **Copy** and **Pop out**. Other content is handled too:

- **XML** is indented.
- **Images, PDFs, archives and other binary** content is not dumped as text: a **Content** tab shows the type and size (and a preview for images) with a **Save to file…** button. Any text response can be written out from the output menu (**Save response body…**).
- The **Request** tab shows exactly what was sent, the redirects that were followed and the time to the first byte next to the total time.
- The **Headers** tab shows the response headers.

### Extract a value into a variable

Configure an **extractor** to grab a field from the JSON response into a dynamic variable (e.g. `data.token → AUTH_TOKEN`), used by the following commands:

```
Authorization: Bearer {{AUTH_TOKEN}}
```

### Assertions

An **assertion** is a check on the response (same format as [execution conditions](hooks.md)): `{{RESPONSE_STATUS}}` *equals* `201`, `{{RESPONSE_BODY}}` *contains* `"ok"`. All of them must pass. When at least one is turned on, the assertions decide whether the request succeeded — so a `404` can be the expected outcome — and a failure names the assertion that did not pass.

Available variables: `RESPONSE_STATUS`, `RESPONSE_TIME_MS`, `RESPONSE_SIZE`, `RESPONSE_CONTENT_TYPE`, `RESPONSE_BODY`, `RESPONSE_HEADER_<NAME>` (upper case, `-` becomes `_`, e.g. `RESPONSE_HEADER_CONTENT_TYPE`), and every extracted variable. They stay available to the next commands and hooks, so a post-hook can run only when `{{RESPONSE_STATUS}}` is not `200`. `RESPONSE_BODY` is the exception: it only exists as `{{RESPONSE_BODY}}` inside Kai (assertions, conditions, command text) and is never exported to the environment of a process — a large body there would break every command that runs afterwards. To hand it to a script, write it explicitly.

### Save the response to a file

On the **Response** page, give a path (variables allowed). Successful (2xx) responses are written there as they arrive instead of being kept in memory — meant for downloads. An error response never overwrites the file.

## Stop, and event streams

The **Stop** button cancels a request in flight. A response of type `text/event-stream` (**Server-Sent Events**) is shown **as the events arrive**, and keeps running until the server closes it or you press Stop — raise the **Timeout** in Connection (it counts the time without data) for slow streams.

## When there is no response

If the server never answers (timeout, refused connection, DNS), the **Request** tab opens with the reason highlighted, the status shows **No response** / **Timed out** instead of a code, and the **Response** tab does not appear. **Headers sent** and **Body sent** each have a **Copy** button; the body can be shown formatted (JSON, in the original key order) or exactly as sent.

## Quick edit

The **Quick edit** button (side bar, or its shortcut) opens the method, URL, body, query, headers and authentication of the selected request without opening the full editor. New HTTP commands start with `Content-Type` and `Accept: application/json` and a JSON body; `Host` is sent automatically from the URL.

## Copy as cURL or code

The **Copy as…** button in the HTTP page puts the request on the clipboard as a **cURL** command, **Python (requests)** or **JavaScript (fetch)**, with the active environment's and the folders' variables already resolved.

## Import

- **Import cURL** — paste a `curl` command; see [cURL import](import-curl.md).
- **File → Import…** also reads a **Postman collection**, an **Insomnia export** and a **HAR** file: see [Export & Import](export-import.md).
- An **OpenAPI** spec: [OpenAPI import](import-openapi.md).

## Example

```yaml
- name: "Create user"
  type: "http"
  http_config:
    method: "POST"
    url: "{{BASE_URL}}/users"
    auth:
      type: "bearer"
      token: "{{API_TOKEN}}"
    body_type: "json"
    body: |
      {"name": "{{NAME}}"}
    env_extractors:
      - json_path: "id"
        env_var: "USER_ID"
    assertions:
      - name: "Created"
        left: "{{RESPONSE_STATUS}}"
        op: "eq"
        right: "201"
```

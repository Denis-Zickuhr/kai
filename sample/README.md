# Samples

Each folder is a project you can import into Kai (**File → Import Project**)
to try a feature by hand without touching your own config. Several are also
exercised by the test suite, so they stay working.

| Folder | What it shows | Needs |
|---|---|---|
| [`http`](http/kai.yml) | The HTTP client: headers/auth inherited from the project and subfolders, query tables, JSON/XML/form/multipart/binary/GraphQL bodies, assertions, token extraction and reuse, redirects, cookies, timeout, image preview, save to file | internet (httpbin.org) |
| [`shell`](shell/kai.yml) | Parameters (text, select, bool, file, `options_script`), dynamic variables, a masked secret, a custom `working_dir`, a background process, a Collection | `bash` |
| [`languages`](languages/kai.yml) | Python and Node written directly in the command, the injected `kai` module, a per-command interpreter; `showcase.py` is a long interactive program using `kip` and `kai` | `python3`, `node` |
| [`kip`](kip/README.md) | Programs that speak KIP: wizards, confirmations, tables, diffs, trees, login with `set-env`, in bash, Python, Node and PHP | `bash` (+ `python3`, `node`, `php`) |
| [`logs`](logs/kai.yml) | Formatted output for JSON and logfmt logs: levels, grouped stack traces, rebuilt multi-line JSON, live stream, filters, a 5,000-line volume test | `bash` |
| [`docs`](docs/README.md) | The documentation viewer: JSON/YAML/XML folding, and project notes of each type | — |
| [`ai`](ai/README.md) | A fake OpenAI-compatible provider to try the AI chat without a key | `python3` |
| [`stop`](stop/README.md) | A Go bridge on its own pty plus a `docker compose` stack, to check that Stop ends everything | `bash`, Go or Docker |

## Trying one

1. Import the folder (e.g. `sample/shell`); the project name comes from its
   `kai.yml`.
2. Run the numbered commands in order — each one isolates a single feature.

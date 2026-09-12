# Hooks (pre/post/cleanup)

A command can fire others **before** (pre), **after** (post), and whenever
it **finishes** (cleanup):

```
[Pre-Hooks]  →  [Main command]  →  [Post-Hooks]  →  (always) [Cleanup]
```

If a **pre-hook** fails (exit ≠ 0, or HTTP ≥ 400), the pipeline **aborts**
and the main command never runs. If a **post-hook** fails, the pipeline is
marked as unsuccessful. **Cleanup** hooks always run — success, failure,
manual stop, or reset — to tear down whatever the command brought up.

## Classic case (auth + token)

1. An HTTP pre-hook authenticates and **extracts the token** (Env Extractor
   → `AUTH_TOKEN`).
2. The main command uses `Authorization: Bearer {{AUTH_TOKEN}}`.

## Folder inheritance

Commands from **ancestor folders** show up as available hooks in
subfolders (marked `⤴ folder`). A utility defined at the root can serve
every command below it, without duplication.

## Execution conditions

A hook can also carry its own **execution conditions** — a guard that
decides whether it actually runs, based on comparing interpolated values
(env vars, `{{$timestamp}}`, literals). Useful for "only run this login
hook if the token is missing or expired". See the
[`kai.json` manifesto](manifesto/kai-json-manifesto.md#8-execution-conditions)
for the full syntax.

## Tip

Duplicate command names in the same folder are disambiguated automatically
(the id gets a `_2`, `_3`, ... suffix), so hooks never "disappear" from an
id collision.

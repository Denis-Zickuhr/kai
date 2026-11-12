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

## Hooks on folders

A **folder** can have hooks too (folder editor → **Hooks** tab): they wrap **every command in that folder and in its
subfolders**. A command's own hooks are added to them.

| Phase | Order |
|---|---|
| **pre** | from the outermost folder to the command: root folder → … → folder → the command's own |
| **post** and **cleanup** | the order unwinds: the command's own, then the folders from the nearest to the outermost |

Nothing is inherited if you don't want it — you have to say so:

- a **subfolder** turns off *Inherit hooks from parent folders*: it ignores the hooks of the folders above it (its own
  hooks, and those of its children, still apply);
- a **command** turns off *Inherit folder hooks* (its **Hooks** tab): it ignores every folder hook and only its own run.

The command's Hooks tab and the subfolder's tab list what arrives from above (read-only), with the folder it comes from.
A command is never a hook of itself: if the folder hook `Login` lives in that same folder, running `Login` does not call
`Login` before itself. A repeated hook runs once per phase. Folder hooks are saved in `kai.yml` by command **name** (top of
the file for the project, on a `folders:` entry for a subfolder; `inherit_hooks: false` / `inherit_folder_hooks: false` to
opt out) — see the [manifesto](../assets/manifesto/kai-manifesto.md#91-hooks).

A command nested inside another command does not inherit the hooks of the folders above (the folder chain is read from
the command's `folder`, like the HTTP defaults).

## Execution conditions

A hook can also carry its own **execution conditions** — a guard that
decides whether it actually runs, based on comparing interpolated values
(env vars, `{{$timestamp}}`, literals). Useful for "only run this login
hook if the token is missing or expired". See the
[Kai manifesto](../assets/manifesto/kai-manifesto.md#92-execution-conditions)
for the full syntax.

## Tip

Duplicate command names in the same folder are disambiguated automatically
(the id gets a `_2`, `_3`, ... suffix), so hooks never "disappear" from an
id collision.

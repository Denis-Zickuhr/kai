# Store

The **Store** is where you find and install things made for Kai: commands, folders, pipelines, whole projects, environments and execution profiles. It is **not a server of ours**. You pick one or more **providers**, and a provider is any place that serves a catalog in the Store format: a Git repository, a plain catalog file, or an HTTP API.

Installing is an **import**: it creates ordinary folders and commands, and nothing runs while installing.

## Turn it on

No provider comes with Kai. Until you add one, there is no **Store** panel and Kai never contacts anything for the Store.

**Settings → Store → + Add**:

| Field | |
|---|---|
| Name | what you call it |
| Location | a Git URL, the path or URL of a catalog file, or an API address |
| Type | Kai suggests one from the location; you can change it |
| Token | optional, for private stores. Stays on this machine and is sent only to the provider's own host |
| Trust | official / verified / community / custom, a label for you |

**Test connection** reads the catalog and shows its name, how many packages it has, and whether the index is signed.

| Type | Location | How it is read |
|---|---|---|
| **File** | a path or URL of one `.yml`/`.json` catalog | one read; the file lists every package |
| **Git repo** | `https://github.com/owner/repo` or any `.git` URL (add `#tag-or-branch` to pick a ref) | `store.yml` and `index.json` over HTTPS for GitHub and GitLab, otherwise (or if that fails) a shallow `git clone` when Git is installed. The catalog may sit at the repository root or in a `dist/` folder |
| **API** | a base URL | `GET {base}/v1/store` and `GET {base}/v1/index` |

Packages are code. Add only providers you trust.

## Install

The **Store** panel (View → Store, shopping-cart icon; it only appears with an enabled provider) has a category rail, a search box and a grid. Opening a card shows the description, versions (incompatible ones are greyed out with the reason), what the package needs and the secrets it declares.

**Install** downloads the files, checks every `sha256`, validates the `kai.yml` and then shows a **preview before anything is written**:

- every command and script in a read-only viewer, and the URLs the HTTP commands call;
- the programs it needs, marked found or not found;
- the choices below;
- the destination folder.

| In the package | What happens |
|---|---|
| `hooks` | **Never installed.** The preview says how many were skipped |
| `cron` schedule | Asked per command, **off** by default |
| `auto_run` | Asked per command, **off** by default |
| `secrets` | Variables created empty and marked secret; you fill them in |

The grid has filters for type and status (not installed, installed, with updates). The **Installed** tab lists everything the Store installed, with details, **Uninstall** (one or several at once) and **Remove from list** for entries whose folder is gone, or for environment and profile packs (what they imported stays).

Updates are checked only when the Store is open or when you click **Refresh**. **Update** replaces the installed folder; if you edited it, Kai offers **Install as a copy** instead (there is no merge). **Uninstall** removes only the folder the install created.

## Safety

- Network providers need HTTPS (plain `http` only for `localhost`). Redirects to another host are refused.
- Each file is checked against its `sha256`; paths that escape the package folder are rejected; there are limits on size and file count.
- An index can be **signed** (ed25519). A provider that declares a key and serves a missing or wrong signature **blocks installs**. The signature covers each `package.yml` hash, which in turn covers its files.
- Descriptions are rendered as Markdown without HTML. Icons are raster images only.

## From the terminal

```
kai store list [--provider <name>] [--json]
kai store search <text>
kai store show <publisher/slug>[@version]
kai store install <publisher/slug>[@version] [--provider <name>] [--yes]
kai store update [<publisher/slug>] [--copy]
```

`install` downloads and verifies in the terminal, prints the same preview, asks about schedules and auto-run (with `--yes` the defaults stay off) and then hands the verified package to the running Kai. `update` with no package only lists what is newer.

## Publish your own

You need no server. Put the files in a Git repository (or one catalog file anywhere) and share the address.

```
store.yml                                   # the catalog header
index.json                                  # generated
packages/<publisher>/<slug>/<version>/package.yml
packages/<publisher>/<slug>/<version>/kai.yml, scripts/…, icon.png
```

`store.yml`:

```yaml
store_format: 1
name: Acme Kai Store
categories:
  - { id: deploy, name: { en: Deploy, pt: Deploy }, icon: rocket }
signing: { algorithm: ed25519, key: "<base64 public key>" }   # optional
```

`package.yml`:

```yaml
package_format: 1
id: acme/ship-to-staging        # publisher/slug, lowercase
kind: pipeline                  # command | folder | pipeline | project | environment | profile
version: 1.2.0
name: Ship to staging
description: { en: "…", pt: "…" }   # Markdown
category: deploy
tags: [git]
author: Acme
license: MIT
kai: { min: 1.0.0-beta.2, requires: [pipelines] }
needs: [git]                    # programs, shown in the preview
secrets: [DEPLOY_TOKEN]         # names only
files:                          # filled in by `kai store pack`
  - { path: kai.yml, sha256: "…" }
```

- `kai.requires` lists capabilities: `http`, `pipelines`, `kip`, `folder_actions`, `hooks`, `cron`, `collections`, `notes`, `responders`. A Kai without one greys the package out.
- `kind: project` can instead point at a repository with `source: { clone: { url, ref, path } }` (use a tag or commit as `ref`; a branch is flagged as "moves"). It is cloned when installed, and the project points at that clone.
- `environment` and `profile` packages carry a configuration package (`kai_export`) and import only that category.

```
kai store validate <dir>                # package.yml + files + hashes
kai store pack <dir>                    # writes files[] with sha256
kai store index <repo> [--catalog file] [--key keyfile]   # index.json, or one catalog file
kai store keygen [file]                 # ed25519 key pair; paste the public key in store.yml
```

`pack` rewrites `package.yml`, so comments in it are not kept. A repository you publish only needs to be committed and shared.

Working examples: `sample/store/` (offline catalog file), `sample/store-github/` (a Git repo with the catalog in `dist/`) and `sample/store-api/` (a small API server with a token and a signed index).

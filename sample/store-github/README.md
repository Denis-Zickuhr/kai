# GitHub-style store demo

A Git repository whose catalog is published under `dist/` (`store.yml` + `index.json` + `packages/`).

## Try it locally

1. `bash sample/store-github/tools/make-repo.sh` (creates `/tmp/kai-store-sample/github-demo`).
2. Settings -> Store -> Add: **Name** `Acme (git)`, **Type** Git repo, **Location** `/tmp/kai-store-sample/github-demo`.
   Add `#v1.0.0` to the end of the location to pin the tag.
3. File -> Store…: four packages. `acme/git-helpers` has 1.0.0 and 1.1.0 (install, then check the update flow).
   `acme/docker-tools` needs `docker`, declares the secret `REGISTRY_TOKEN` and has a schedule (asked, off by default).
   `acme/team-envs` is an environment pack. `acme/python-report` uses Python.

## Try it on GitHub

Push the **contents of `repo/`** to a GitHub repository, then add `https://github.com/<you>/<repo>` as a Git repo provider.
Kai reads `store.yml` from the repo root and, if it is not there, from `dist/` (raw files over HTTPS, with a shallow
`git clone` as the fallback). A token is only needed for a private repo (it is sent to the provider's host only).

If you edit a payload file, update its `sha256` in `package.yml` and the manifest hash in `index.json`
(or run `kai store pack` and `kai store index` on a copy that has a `packages/` folder at the repo root).

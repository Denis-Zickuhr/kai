# Store sample (offline)

A working Kai Store provider that needs no network.

1. Settings → Store → **+ Add**
2. Location: the full path of `catalog.yml` in this folder (type **File**), then **Test connection**.
3. File → Store… opens it.

| Package | What it shows |
|---|---|
| `demo/hello-command` | the simplest install |
| `demo/ship-to-staging` | pipeline payload, `needs`, a secret name (created empty) |
| `demo/api-starter` | a folder of HTTP commands |
| `demo/scheduled-ping` | cron + auto-run + a hook: the preview asks, hooks are skipped |
| `demo/needs-the-future` | incompatible (greyed out, with the reason) |
| `demo/bad-hash` | a file changed after hashing: install blocked |
| `demo/app-by-reference` | `source.clone`; run `tools/make-clone-fixture.sh` first |

`catalog-signed.json` is the same catalog signed with a throwaway key (the private key is `tools/test-key.txt`, public
only to demonstrate): add it as a File provider to see "signed". Edit one character of it and it turns "invalid signature"
and installs are blocked.

The tests install from this folder. If you change a payload file, update its `sha256` in `package.yml`, and the package's
`package.yml` hash in `catalog.yml` and `catalog-signed.json` (the latter then needs re-signing).

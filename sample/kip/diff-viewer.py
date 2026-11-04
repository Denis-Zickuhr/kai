"""Commit viewer — KIP demo 21: the `diff` block.

Pick a commit in a table (cell formats: mono hash, badge for the kind, muted author/date) and its diff opens:
several files, a rename, a binary file, a new file, changed words highlighted, JSON/YAML colored. Switch between
unified and side by side: Kai remembers your choice for this command. ↑ ↓ (or F7) walk the changes.

Fake data only: nothing here touches git. Run by Kai as Python code (`language: python`, `kip: true`).
"""
import kip

COMMITS = [
    {
        "hash": "9da9e13", "subject": "fix(auth): force login when there is no token", "author": "Ana",
        "when": "2 h ago", "kind": {"text": "fix", "tone": "warning"},
        "patch": """diff --git a/src/auth/session.ts b/src/auth/session.ts
--- a/src/auth/session.ts
+++ b/src/auth/session.ts
@@ -18,8 +18,11 @@ export class Session
   constructor(private store: TokenStore) {}

   async refresh(): Promise<string> {
     const token = await this.store.read();
-    if (!token) throw new Error("no token");
+    if (!token) {
+      log.warn("session: no token, forcing login");
+      return this.login();
+    }
     const res = await http.post("/auth/refresh", { token });
-    return res.body.access;
+    return res.body.access_token;
   }
@@ -61,4 +64,4 @@ logout()
   logout() {
-    this.store.clear();
+    this.store.clear(true);
   }
 }
""",
    },
    {
        "hash": "52e39ad", "subject": "chore(deploy): scale billing to 6 replicas", "author": "Bruno",
        "when": "yesterday", "kind": {"text": "chore", "tone": "neutral"},
        "patch": """diff --git a/config/deploy.yml b/config/deploy.yml
--- a/config/deploy.yml
+++ b/config/deploy.yml
@@ -5,7 +5,8 @@ services:
   billing-api:
     image: registry.local/billing:1.4.0
-    replicas: 2
+    replicas: 6
+    healthcheck: /healthz
     resources:
-      memory: 512Mi
+      memory: 1Gi
       cpu: 500m
     env_file: .env.prod
diff --git a/config/flags.json b/config/flags.json
--- a/config/flags.json
+++ b/config/flags.json
@@ -1,5 +1,5 @@
 {
-  "new_checkout": false,
+  "new_checkout": true,
   "legacy_tax": true,
-  "log_level": "debug"
+  "log_level": "warn"
 }
""",
    },
    {
        "hash": "585439b", "subject": "docs: move the auth guide and add the logo", "author": "Carla",
        "when": "3 days ago", "kind": {"text": "docs", "tone": "info"},
        "patch": """diff --git a/README-auth.md b/docs/auth.md
similarity index 88%
rename from README-auth.md
rename to docs/auth.md
--- a/README-auth.md
+++ b/docs/auth.md
@@ -1,4 +1,5 @@
 # Sessions and tokens

-Refresh renews the token.
+Refresh renews the access token before it expires.
+A missing token sends the user back to the login screen.

diff --git a/docs/logo.png b/docs/logo.png
new file mode 100644
Binary files /dev/null and b/docs/logo.png differ
diff --git a/docs/index.md b/docs/index.md
new file mode 100644
--- /dev/null
+++ b/docs/index.md
@@ -0,0 +1,4 @@
+# Docs
+
+- [Sessions and tokens](auth.md)
+- [Deploying](deploy.md)
""",
    },
]

ROWS = [{"hash": c["hash"], "subject": c["subject"], "kind": c["kind"], "author": c["author"], "when": c["when"]}
        for c in COMMITS]

kip.hello(title="Commit viewer (demo)", version="1.0")
while True:
    picked = kip.prompt([{
        "name": "commit", "type": "table", "label": "Recent commits", "row_key": "hash", "required": True,
        "default": COMMITS[0]["hash"],
        "columns": [
            {"key": "hash", "label": "Commit", "format": "copy"},
            {"key": "kind", "label": "Kind", "format": "badge"},
            {"key": "subject", "label": "Subject"},
            {"key": "author", "label": "Author", "format": "muted"},
            {"key": "when", "label": "When", "format": "muted"},
        ],
        "rows": ROWS,
    }], id="pick", title="Which commit?", submit_label="Show the diff", remember=False)
    commit = next(c for c in COMMITS if c["hash"] == picked["commit"])
    kip.diff(id="commit", title="%s · %s" % (commit["hash"], commit["subject"]), patch=commit["patch"])
    if not kip.confirm("Look at another commit?", id="more", confirm_label="Pick another", cancel_label="Done"):
        break

kip.done(title="Done", text="Your unified / side-by-side choice is remembered for this command.")

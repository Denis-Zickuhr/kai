"""Git review, round-2 components — KIP demo 19.

Branches table with row actions -> the diff of the chosen branch -> a commit form that picks hunks ->
a build with step output and several progress bars -> a result screen with stats, kv and code.

It shows the components of the second round:
  * `table` cells with formats (mono, badge, muted, number) and `row_actions` (Sync, Open PR, Delete with an
    inline confirmation; `_actions` keeps `main` from being deleted);
  * a `diff` block (unified or side by side: the user picks, Kai remembers);
  * a `diff` field (which hunks go into the commit), a `textarea` with `max_length`, a `segmented` select;
  * `step` with `log`, `progress` with `id`;
  * `stats`, `kv` and `code` on the result screen.

Fake data only: nothing here touches git. Run by Kai as Python code (`language: python`, `kip: true`).
"""
import time

import kip

PATCH = """diff --git a/src/auth/session.ts b/src/auth/session.ts
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
diff --git a/config/deploy.yml b/config/deploy.yml
--- a/config/deploy.yml
+++ b/config/deploy.yml
@@ -7,3 +7,4 @@ services:
   billing-api:
     image: registry.local/billing:1.4.0
-    replicas: 2
+    replicas: 6
+    healthcheck: /healthz
diff --git a/docs/auth.md b/docs/auth.md
new file mode 100644
--- /dev/null
+++ b/docs/auth.md
@@ -0,0 +1,3 @@
+# Sessions and tokens
+
+Refresh renews the access token before it expires.
"""

BRANCHES = [
    {"name": "main", "state": {"text": "default", "tone": "info"}, "updated": "2 h ago", "ab": "—", "_actions": ["sync"]},
    {"name": "feat/login", "state": {"text": "current", "tone": "accent"}, "updated": "14 min ago", "ab": "↑3"},
    {"name": "feat/payments", "state": {"text": "PR #212", "tone": "success"}, "updated": "yesterday", "ab": "↑7 ↓2"},
    {"name": "fix/cart-total", "state": "merged", "updated": "3 days ago", "ab": "—"},
    {"name": "refactor/api", "state": {"text": "conflict", "tone": "error"}, "updated": "1 week ago", "ab": "↑12 ↓30"},
]


def branches_field():
    return {
        "name": "branch", "type": "table", "label": "Branches", "required": True, "row_key": "name",
        "default": "feat/login",
        "columns": [
            {"key": "name", "label": "Branch", "format": "mono"},
            {"key": "state", "label": "State", "format": "badge"},
            {"key": "updated", "label": "Updated", "format": "muted"},
            {"key": "ab", "label": "Ahead / behind", "format": "number"},
        ],
        "rows": BRANCHES,
        "row_actions": [
            {"id": "sync", "label": "Sync", "icon": "refresh-cw", "description": "Rebase onto origin/main"},
            {"id": "pr", "label": "Open PR", "icon": "git-pull-request"},
            {"id": "delete", "label": "Delete", "icon": "trash-2", "danger": True,
             "confirm": {"text": "The local branch is gone for good.", "confirm_label": "Delete", "cancel_label": "Keep"}},
        ],
    }


def on_row_action(action, row, values):
    time.sleep(0.4)
    if action == "sync":
        return {"title": "Sync · " + row, "text": "Rebased onto `origin/main`, 2 commits replayed."}
    if action == "pr":
        return {"title": "Pull request · " + row, "text": "PR **#219** opened as a draft (demo)."}
    # delete: drop the row and repaint the table with a spontaneous patch (no seq)
    BRANCHES[:] = [b for b in BRANCHES if b["name"] != row]
    kip.patch("branches", fields=[branches_field()])
    return {"title": "Delete · " + row, "text": "Branch **%s** deleted." % row}


kip.hello(title="git review (demo)", version="0.3.0")

values = kip.prompt([branches_field()], id="branches", title="Pick a branch",
                    description="Demo data only: nothing touches git. Hover a row for its actions.",
                    submit_label="Review changes", on_row_action=on_row_action)
branch = values["branch"]

kip.diff(id="wc", title="Changes on " + branch, patch=PATCH)
commit = kip.prompt([
    {"name": "msg", "type": "textarea", "label": "Message", "max_length": 72, "required": True,
     "default": "fix(auth): force login when there is no token"},
    {"name": "hunks", "type": "diff", "label": "What goes in", "patch": PATCH, "required": True},
    {"name": "after", "type": "select", "label": "Then", "style": "segmented", "default": "push",
     "options": [{"value": "push", "label": "Push"}, {"value": "local", "label": "Keep local"}]},
], id="commit", title="Commit", submit_label="Commit", back=False, remember=False)

kip.steps("build", [
    {"id": "deps", "label": "Dependencies"},
    {"id": "compile", "label": "Compiling"},
    {"id": "pack", "label": "Package"},
], title="Build")
kip.step("build", "deps", "success", "0.8 s")
kip.step("build", "compile", "running")
for i in range(38, 44):
    kip.step("build", "compile", log="[%d/118] Building CXX object src/core/file%d.cpp.o" % (i, i))
    time.sleep(0.1)
kip.step("build", "compile", "success", "118 objects")
kip.step("build", "pack", "running")
for pct in (20, 60, 100):
    kip.progress(pct, "kai-1.4.0-linux-x64.tar.gz", id="linux")
    kip.progress(min(100, pct + 20), "kai-1.4.0-windows-x64.zip", id="win")
    time.sleep(0.15)
kip.step("build", "pack", "success")

hunks = len(commit["hunks"])
kip.stats([
    {"label": "Hunks", "value": str(hunks), "detail": "of 4 in the diff", "tone": "success"},
    {"label": "Tests", "value": "412", "detail": "all passed", "tone": "success"},
    {"label": "Duration", "value": "3m 12s", "detail": "41 s more than last time", "tone": "warning"},
])
kip.kv([
    {"key": "Branch", "value": branch, "format": "mono", "copy": True},
    {"key": "Commit", "value": "9da9e13", "format": "mono", "copy": True},
    {"key": "Pushed", "value": {"text": "yes" if commit["after"] == "push" else "no",
                                "tone": "success" if commit["after"] == "push" else "neutral"}, "format": "badge"},
    {"key": "Pull request", "value": "https://example.com/pr/219", "format": "link"},
])
kip.code(commit["msg"] + "\n\nHunks:\n" + "\n".join(commit["hunks"]), title="COMMIT_EDITMSG", max_lines=8)
kip.done(title="Committed to " + branch, text="%d hunk(s) in the commit." % hunks,
         actions=[{"type": "copy", "label": "Copy hash", "value": "9da9e13"}])

"""Partial commit — KIP demo 26: the `diff` FIELD.

A commit form: a message with `max_length` (the counter turns red past 72) and a diff where you tick the hunks that go
into the commit. The answer is the list of ids ("path@n"); the program rebuilds the patch with only those hunks —
what `git apply --cached` would take — and shows it with `code`, plus `stats` and `kv`.

Fake data only: nothing here touches git. Run by Kai as Python code (`language: python`, `kip: true`).
"""
import kip

PATCH = """diff --git a/src/auth/session.ts b/src/auth/session.ts
--- a/src/auth/session.ts
+++ b/src/auth/session.ts
@@ -21,3 +21,5 @@ export class Session
     const token = await this.store.read();
-    if (!token) throw new Error("no token");
+    if (!token) {
+      return this.login();
+    }
     const res = await http.post("/auth/refresh", { token });
@@ -61,4 +64,4 @@ logout()
   logout() {
-    this.store.clear();
+    this.store.clear(true);
   }
 }
diff --git a/config/deploy.yml b/config/deploy.yml
--- a/config/deploy.yml
+++ b/config/deploy.yml
@@ -7,2 +7,3 @@ services:
     image: registry.local/billing:1.4.0
-    replicas: 2
+    replicas: 6
+    healthcheck: /healthz
diff --git a/debug.log b/debug.log
new file mode 100644
--- /dev/null
+++ b/debug.log
@@ -0,0 +1,2 @@
+temporary output
+do not commit me
"""


def split_patch(patch):
    """[(path, header_lines, [hunk_lines, ...]), ...] — enough for a well-formed git patch."""
    files = []
    for chunk in patch.split("diff --git ")[1:]:
        lines = ("diff --git " + chunk).rstrip("\n").split("\n")
        header, hunks, path = [], [], None
        for line in lines:
            if line.startswith("@@"):
                hunks.append([line])
            elif hunks:
                hunks[-1].append(line)
            else:
                header.append(line)
                if line.startswith("+++ b/"):
                    path = line[6:]
        files.append((path, header, hunks))
    return files


kip.hello(title="Partial commit (demo)", version="1.0")
values = kip.prompt([
    {"name": "msg", "type": "textarea", "label": "Message", "max_length": 72, "required": True,
     "default": "fix(auth): send the user to login when the token is missing"},
    {"name": "hunks", "type": "diff", "label": "What goes into the commit", "patch": PATCH, "required": True},
], id="commit", title="Commit on feat/login", submit_label="Commit", remember=False)

chosen = set(values["hunks"])
out, picked, total = [], 0, 0
for path, header, hunks in split_patch(PATCH):
    keep = [h for i, h in enumerate(hunks) if "%s@%d" % (path, i) in chosen]
    total += len(hunks)
    picked += len(keep)
    if keep:
        out += header + [line for hunk in keep for line in hunk]
staged = "\n".join(out)

kip.stats([
    {"label": "Hunks", "value": "%d / %d" % (picked, total), "detail": "in the commit", "tone": "success"},
    {"label": "Left out", "value": str(total - picked), "detail": "still unstaged",
     "tone": "warning" if picked < total else "success"},
])
kip.kv([
    {"key": "Message", "value": values["msg"]},
    {"key": "Command", "value": "git apply --cached staged.patch && git commit -F msg.txt", "format": "mono", "copy": True},
])
kip.code(staged, title="staged.patch", max_lines=12)
kip.done(title="Commit ready (demo)", text="Nothing was committed: this is the patch the commit would take.")

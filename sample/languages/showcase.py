"""Kai showcase: one long Python program that exercises the `kip` and `kai` modules.

Run it from Kai (command "8. Showcase" in sample/languages, a Python command with KIP on) —
`kip` and `kai` are injected by Kai, so there is nothing to install. The program is a menu
you keep coming back to (every tool returns to it), and each tool shows something different:

  * Project scaffolder   a 4-step wizard: Back at every step, dependent fields (watch/patch),
                         chips, validation (`invalid`), table review, a danger confirm, a live
                         checklist, retry after a failure, files really written to disk
  * Environments         kai.env.list / active / use, and restoring the previous one
  * Process monitor      kai.ps / kai.kill with a refresh loop and a safety net
  * Notification lab     kai.notify with levels, counts and a cancellable progress bar
  * Run another command  kai.commands / kai.run
  * Session report       markdown + table built from everything you did

If the Kai app can't be reached the `kai` calls degrade to warnings; the rest keeps working.
Cancel (anywhere) ends the run; Back at the first step of a tool returns to the menu.
"""
import datetime
import hashlib
import json
import os
import re
import shutil
import tempfile
import time
from pathlib import Path

import kai
import kip

OUT_BASE = Path(tempfile.gettempdir()) / "kai-showcase"
MARKER = ".kai-showcase"  # only folders holding this file are ever overwritten
SESSION = {"events": [], "projects": [], "started": time.time()}

# --------------------------------------------------------------------------- helpers


def remember(event, **data):
    SESSION["events"].append({"at": datetime.datetime.now().strftime("%H:%M:%S"), "event": event, **data})
    kip.log("[showcase]", event, json.dumps(data, default=str))


def kai_call(fn, *args, **kwargs):
    """Call the kai module; return (ok, value). A failure shows ONE warning and never aborts."""
    try:
        return True, fn(*args, **kwargs)
    except kai.Error as error:
        remember("kai-error", call=getattr(fn, "__name__", "call"), error=str(error))
        kip.message("Kai app call failed: %s" % error, level="warning")
        return False, None


def notify(message, title="Kai showcase", level="info"):
    ok, _ = kai_call(kai.notify, message, title=title, level=level)
    return ok


def ancestors():
    """PIDs of this process and its parents (so the process monitor never kills this very session)."""
    pids, pid = set(), os.getpid()
    while pid > 1 and pid not in pids:
        pids.add(pid)
        try:
            with open("/proc/%d/stat" % pid) as stat:
                pid = int(stat.read().rsplit(")", 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return pids


def slugify(text):
    return re.sub(r"-{2,}", "-", re.sub(r"[^a-z0-9]+", "-", text.lower())).strip("-")


def human(size):
    for unit in ("B", "KB", "MB"):
        if size < 1024:
            return "%d %s" % (size, unit)
        size //= 1024
    return "%d GB" % size


# --------------------------------------------------------------------------- menu

TOOLS = [
    ("scaffold", "Project scaffolder", "Wizard with Back, dependent fields, chips, review, checklist and retry"),
    ("envs", "Environments", "Read and switch Kai's active environment (kai.env)"),
    ("procs", "Process monitor", "List and stop what Kai is running (kai.ps / kai.kill)"),
    ("notify", "Notification lab", "Send tray notifications with levels, counts and progress"),
    ("runner", "Run another Kai command", "Pick a registered command and start it (kai.commands / kai.run)"),
    ("report", "Session report", "Markdown + table of everything you did so far"),
    ("quit", "Quit", "Finish the session"),
]


def menu(last):
    values = kip.prompt([
        {"name": "tool", "type": "list", "label": "What do you want to try?", "required": True,
         "default": last, "options": [{"value": k, "label": l, "description": d} for k, l, d in TOOLS]},
    ], id="menu", title="Kai showcase",
        description="Every tool comes back here. Cancel ends the run.", submit_label="Open", remember=False)
    return values["tool"]


# --------------------------------------------------------------------------- 1. scaffolder

LANGUAGES = {
    "python": {"label": "Python", "ext": "py", "frameworks": ["none", "fastapi", "flask", "click"]},
    "node": {"label": "Node", "ext": "js", "frameworks": ["none", "express", "fastify", "commander"]},
    "go": {"label": "Go", "ext": "go", "frameworks": ["none", "gin", "cobra"]},
    "rust": {"label": "Rust", "ext": "rs", "frameworks": ["none", "axum", "clap"]},
}
TEMPLATES = [
    ("service", "HTTP service", "A small API with a health endpoint"),
    ("cli", "Command-line tool", "Argument parsing and exit codes"),
    ("library", "Library", "Importable package with a public API"),
    ("worker", "Background worker", "A loop that processes jobs"),
    ("bot", "Chat bot", "Handlers for messages"),
    ("etl", "Data pipeline", "Extract, transform, load"),
    ("game", "Game prototype", "A tick loop and a renderer"),
    ("blank", "Blank", "An empty project with just the metadata"),
]
TAGS = ["api", "cli", "web", "data", "ml", "infra", "docs", "internal", "oss", "prototype", "security", "mobile"]
FEATURES = [
    {"name": "git", "label": "Git repository files", "default": True},
    {"name": "tests", "label": "Test skeleton", "default": True},
    {"name": "docker", "label": "Dockerfile", "default": False},
    {"name": "ci", "label": "CI workflow", "default": False},
    {"name": "license", "label": "LICENSE file", "default": True},
    {"name": "fail", "label": "Simulate a failure in the tests step", "description": "To try the retry loop"},
]


def framework_field(language):
    options = LANGUAGES[language]["frameworks"]
    return {"name": "framework", "type": "select", "label": "Framework", "options": options,
            "default": options[0], "group": "Stack"}


def target_field(name, base):
    return {"name": "target", "type": "text", "label": "Target folder (computed)", "group": "Stack",
            "description": "Follows the name; the output folder is chosen in the next step",
            "default": str(Path(base) / (slugify(name) or "<name>"))}


def basics_fields(a):
    language = a.get("language", "python")
    return [
        {"name": "name", "type": "text", "label": "Project name", "required": True, "watch": True,
         "placeholder": "my-awesome-service", "default": a.get("name", ""), "group": "Identity",
         "description": "Lowercase letters, digits and dashes (3-30)."},
        {"name": "template", "type": "list", "label": "Template", "options": [
            {"value": v, "label": l, "description": d} for v, l, d in TEMPLATES],
         "page_size": 4, "searchable": True, "default": a.get("template", "service"), "group": "Identity"},
        {"name": "language", "type": "select", "label": "Language", "watch": True, "group": "Stack",
         "options": [{"value": k, "label": v["label"]} for k, v in LANGUAGES.items()], "default": language},
        dict(framework_field(language), **({"default": a["framework"]}
                                           if a.get("framework") in LANGUAGES[language]["frameworks"] else {})),
        target_field(a.get("name", ""), a.get("output", OUT_BASE)),
        {"name": "features", "type": "flags", "label": "Include", "group": "Extras",
         "options": [dict(f, default=a.get("features", {}).get(f["name"], f.get("default", False))) for f in FEATURES]},
        {"name": "tags", "type": "list", "label": "Tags", "multiple": True, "searchable": True, "group": "Extras",
         "options": TAGS, "default": a.get("tags", ["prototype"]), "page_size": 6,
         "description": "Up to five."},
    ]


def basics_changed(field, values):
    """watch: language -> framework options; name -> computed target."""
    if field == "language":
        return [framework_field(values.get("language") or "python")]
    if field == "name":
        return [target_field(values.get("name", ""), OUT_BASE)]
    return []


def basics_valid(values):
    errors = {}
    name = values.get("name", "")
    if not re.fullmatch(r"[a-z][a-z0-9-]{2,29}", name):
        errors["name"] = "Use 3-30 chars: lowercase letters, digits, dashes; start with a letter."
    if len(values.get("tags") or []) > 5:
        errors["tags"] = "Pick at most five tags."
    return errors


def basics_chip(chip, values):
    """chips: inert actions that never leave the step."""
    if chip == "check":
        folder = OUT_BASE / slugify(values.get("name", ""))
        if folder.exists():
            return {"state": "error", "title": "Name taken",
                    "text": "`%s` already exists. Continue to be asked about overwriting it." % folder}
        return {"state": "success", "title": "Name is free", "text": "`%s` does not exist yet." % folder}
    if chip == "ideas":
        stems = ["atlas", "beacon", "cinder", "delta", "ember", "flux", "harbor", "ion"]
        base = LANGUAGES.get(values.get("language") or "python", {"label": "App"})["label"].lower()
        return {"state": "success", "title": "Name ideas",
                "text": "\n".join("- `%s-%s`" % (stem, base) for stem in stems[:4])}
    if chip == "kai":
        ok_env, active = kai_call(kai.env.active)
        ok_cmds, names = kai_call(kai.commands)
        if not (ok_env and ok_cmds):
            return {"state": "error", "title": "Kai app", "text": "Could not reach the Kai app."}
        return {"state": "success", "title": "Asked the Kai app",
                "text": "Active environment: **%s**\n\n%d registered command(s)." % (active or "-", len(names))}
    return {"state": "error", "text": "Unknown chip %r" % chip}


def ask_basics(a):
    chips = [
        {"id": "check", "label": "Check name", "icon": "search", "requires": ["name"]},
        {"id": "ideas", "label": "Name ideas", "icon": "sparkles"},
        {"id": "kai", "label": "Ask Kai", "icon": "info",
         "description": "Reads the active environment and the command list from the app"},
    ]
    return kip.prompt(basics_fields(a), id="sc-basics", title="1/4 · Basics",
                      description="Change the language to see the framework list follow it.",
                      on_change=basics_changed, on_chip=basics_chip, validate=basics_valid,
                      chips=chips, back=True, submit_label="Next")


def ask_details(a):
    fields = [
        {"name": "version", "type": "text", "label": "Version", "default": a.get("version", "0.1.0"),
         "required": True, "group": "Metadata"},
        {"name": "description", "type": "textarea", "label": "Description", "group": "Metadata",
         "default": a.get("description", ""), "placeholder": "What is it for?"},
        {"name": "license", "type": "select", "label": "License", "group": "Metadata",
         "options": ["MIT", "Apache-2.0", "GPL-3.0", "Proprietary"], "default": a.get("license", "MIT")},
        {"name": "due", "type": "date", "label": "First release", "mode": "date", "group": "Planning"},
        {"name": "budget", "type": "number", "label": "Budget (hours)", "min": 0, "max": 500, "step": 0.5,
         "decimals": 1, "default": a.get("budget", 40), "group": "Planning"},
        {"name": "token", "type": "secret", "label": "Registry token (optional)", "group": "Planning",
         "description": "Never remembered and masked in the protocol inspector."},
        {"name": "output", "type": "folderpick", "label": "Output folder", "group": "Where",
         "initial_dir": str(OUT_BASE.parent), "default": str(a.get("output", OUT_BASE))},
    ]

    def valid(values):
        errors = {}
        if not re.fullmatch(r"\d+\.\d+\.\d+", values.get("version", "")):
            errors["version"] = "Use semantic versioning, e.g. 1.2.3."
        if values.get("due") and values["due"] < datetime.date.today().isoformat():
            errors["due"] = "The release can't be in the past."
        return errors

    return kip.prompt(fields, id="sc-details", title="2/4 · Details", validate=valid, back=True,
                      submit_label="Review")


def plan_files(a):
    lang = LANGUAGES[a["language"]]
    ext, feats = lang["ext"], a["features"]
    files = {
        "README.md": "# %s\n\n%s\n\n- language: %s (%s)\n- template: %s\n" % (
            a["name"], a.get("description") or "No description.", lang["label"], a["framework"], a["template"]),
        "project.json": json.dumps({k: a[k] for k in ("name", "version", "language", "framework", "template", "tags", "license")},
                                   indent=2),
        "src/main.%s" % ext: "// %s entry point (%s)\n" % (a["name"], a["template"]),
    }
    if feats.get("git"):
        files[".gitignore"] = "__pycache__/\nnode_modules/\ntarget/\n.env\n"
    if feats.get("tests"):
        files["tests/test_smoke.%s" % ext] = "// smoke test for %s\n" % a["name"]
    if feats.get("docker"):
        files["Dockerfile"] = "FROM scratch\nCOPY . /app\n"
    if feats.get("ci"):
        files[".ci/workflow.yml"] = "name: ci\non: [push]\n"
    if feats.get("license"):
        files["LICENSE"] = "%s License\n\nCopyright (c) %d\n" % (a["license"], datetime.date.today().year)
    return files


def review(a):
    root = Path(a["output"]) / slugify(a["name"])
    files = plan_files(a)
    exists = root.exists()
    kip.table([{"key": "field", "label": "Field"}, {"key": "value", "label": "Value"}], [
        {"field": "Name", "value": a["name"]},
        {"field": "Stack", "value": "%s / %s" % (LANGUAGES[a["language"]]["label"], a["framework"])},
        {"field": "Template", "value": a["template"]},
        {"field": "Tags", "value": ", ".join(a.get("tags") or []) or "-"},
        {"field": "Version / license", "value": "%s / %s" % (a["version"], a["license"])},
        {"field": "First release", "value": a.get("due") or "-"},
        {"field": "Budget", "value": "%s h" % a.get("budget")},
        {"field": "Token", "value": "••••" if a.get("token") else "-"},
        {"field": "Folder", "value": str(root)},
    ], id="sc-summary", title="Summary")
    kip.markdown("**%d file(s) will be written:**\n\n%s" % (
        len(files), "\n".join("- `%s`" % name for name in sorted(files))))
    return kip.confirm(
        ("`%s` already exists and holds a previous showcase run: it will be **replaced**." % root) if exists
        else "Create %d files in `%s`?" % (len(files), root),
        id="sc-confirm", title="Create the project?", danger=exists, back=True,
        confirm_label="Replace" if exists else "Create", cancel_label="Not now")


def build(a):
    """Write the project for real, with a live checklist. Returns True on success."""
    root = Path(a["output"]) / slugify(a["name"])
    files = plan_files(a)
    while True:
        items = [
            {"id": "prepare", "label": "Prepare the folder"},
            {"id": "write", "label": "Write %d files" % len(files)},
            {"id": "tests", "label": "Run the test skeleton"},
            {"id": "hash", "label": "Fingerprint the result"},
            {"id": "register", "label": "Register with Kai"},
        ]
        kip.steps("sc-run", items, title="Building %s" % a["name"])
        failed = None
        current = "prepare"
        try:
            kip.step("sc-run", "prepare", "running")
            if root.exists():
                if not (root / MARKER).exists():
                    raise RuntimeError("%s exists and is not a showcase folder: refusing to touch it" % root)
                shutil.rmtree(root)
            root.mkdir(parents=True)
            (root / MARKER).write_text(str(time.time()))
            kip.step("sc-run", "prepare", "success", str(root))

            current = "write"
            kip.step("sc-run", "write", "running")
            for i, (name, content) in enumerate(sorted(files.items()), 1):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
                kip.progress(round(100 * i / len(files)), "Writing %s" % name)
                time.sleep(0.12)
            kip.step("sc-run", "write", "success", "%d files" % len(files))

            current = "tests"
            kip.step("sc-run", "tests", "running")
            time.sleep(0.4)
            if a["features"].get("fail"):
                failed = "tests"
                raise RuntimeError("test_smoke failed: expected 1 got 0 (simulated)")
            if not a["features"].get("tests"):
                kip.step("sc-run", "tests", "skipped", "no test skeleton selected")
            else:
                kip.step("sc-run", "tests", "success", "1 passed")

            current = "hash"
            kip.step("sc-run", "hash", "running")
            manifest = {}
            for path in sorted(p for p in root.rglob("*") if p.is_file() and p.name != MARKER):
                manifest[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()[:16]
            (root / "MANIFEST.json").write_text(json.dumps(manifest, indent=2))
            kip.step("sc-run", "hash", "success", "%d digests" % len(manifest))

            current = "register"
            kip.step("sc-run", "register", "running")
            if notify("Project %s created in %s" % (a["name"], root), title="Scaffolder"):
                kip.step("sc-run", "register", "success", "tray notification sent")
            else:
                kip.step("sc-run", "register", "skipped", "Kai app unreachable")
        except Exception as error:  # noqa: BLE001 - shown to the user, then offered a retry
            kip.step("sc-run", current, "error", str(error))
            remember("scaffold-failed", project=a["name"], error=str(error))
            kip.progress(None, None)
            if not kip.confirm("**%s**\n\nTry again%s?" % (error, " without the simulated failure" if failed else ""),
                               id="sc-retry", title="The build failed", confirm_label="Retry", cancel_label="Give up"):
                return False
            a["features"]["fail"] = False
            continue
        break

    SESSION["projects"].append({"name": a["name"], "path": str(root), "files": len(files), "stack": a["language"]})
    remember("scaffold-done", project=a["name"], path=str(root))
    kip.set_env("SHOWCASE_LAST_PROJECT", str(root))
    kip.table([{"key": "file", "label": "File"}, {"key": "size", "label": "Size"}, {"key": "sha", "label": "sha256"}],
              [{"file": n, "size": human((root / n).stat().st_size), "sha": d} for n, d in sorted(manifest.items())],
              id="sc-files", title="Result")
    kip.message("Project created in %s (exported as SHOWCASE_LAST_PROJECT)" % root, level="success")
    return True


def scaffold():
    answers, step = {}, 0
    while True:
        try:
            if step == 0:
                answers.update(ask_basics(answers))
                answers["features"] = dict(answers["features"])
                step = 1
            elif step == 1:
                answers.update(ask_details(answers))
                step = 2
            elif step == 2:
                if not review(answers):
                    remember("scaffold-declined", project=answers["name"])
                    kip.message("Nothing was written.", level="info")
                    return
                step = 3
            else:
                build(answers)
                return
        except kip.Back:
            if step == 0:
                return
            step -= 1  # the previous screen comes back pre-filled


# --------------------------------------------------------------------------- 2. environments


def environments():
    ok, names = kai_call(kai.env.list)
    ok2, active = kai_call(kai.env.active)
    if not (ok and ok2):
        return
    if not names:
        kip.message("Kai has no environments yet.", level="info")
        return
    original, switched = active, 0
    while True:
        kip.table([{"key": "name", "label": "Environment"}, {"key": "state", "label": ""}],
                  [{"name": n, "state": "● active" if n == active else ""} for n in names], title="Environments")
        values = kip.prompt([
            {"name": "env", "type": "select", "label": "Switch to", "options": names, "default": active, "required": True},
            {"name": "options", "type": "flags", "label": "When I leave", "options": [
                {"name": "restore", "label": "Restore the environment I started with", "default": True}]},
        ], id="envs-pick", title="Environments", description="Active now: %s" % active, back=True,
            submit_label="Switch")
        choice = values["env"]
        if choice != active:
            ok, message = kai_call(kai.env.use, choice)
            if ok:
                active, switched = choice, switched + 1
                remember("env-switch", to=choice)
                kip.message("Active environment is now %s. %s" % (choice, message or ""), level="success")
        if not kip.confirm("Switch again?", id="envs-again", title="Environments", confirm_label="Switch again",
                           cancel_label="I'm done"):
            break
        remember("env-loop", switched=switched)
    if values["options"]["restore"] and active != original:
        kai_call(kai.env.use, original)
        kip.message("Restored %s." % original, level="info")


# --------------------------------------------------------------------------- 3. processes


def processes():
    mine = ancestors()
    while True:
        ok, items = kai_call(kai.ps)
        if not ok:
            return
        rows = [{"id": str(p.get("id")), "name": p.get("name"), "pid": p.get("pid"), "status": p.get("status"),
                 "note": "this session" if p.get("pid") in mine else ""} for p in items if isinstance(p, dict)]
        fields = [{"name": "action", "type": "select", "label": "Action", "required": True, "default": "refresh",
                   "options": [{"value": "refresh", "label": "Refresh the list"},
                               {"value": "kill", "label": "Stop the selected processes"}]}]
        if rows:
            fields.append({"name": "targets", "type": "table", "label": "Processes", "multiple": True,
                           "searchable": True, "page_size": 8, "row_key": "id", "rows": rows, "columns": [
                               {"key": "name", "label": "Command"}, {"key": "pid", "label": "PID"},
                               {"key": "status", "label": "Status"}, {"key": "note", "label": ""}]})
        values = kip.prompt(fields, id="procs", title="%d process(es) tracked by Kai" % len(rows),
                            back=True, submit_label="Go")
        if values["action"] != "kill":
            continue
        chosen = [r for r in rows if r["id"] in (values.get("targets") or [])]
        safe = [r for r in chosen if r["pid"] not in mine]
        if len(safe) != len(chosen):
            kip.message("This very session was left out: stopping it would end the showcase.", level="warning")
        if not safe:
            kip.message("Nothing to stop.", level="info")
            continue
        if kip.confirm("Stop **%s**?" % ", ".join(r["name"] for r in safe), id="procs-kill", title="Stop processes",
                       danger=True, confirm_label="Stop"):
            for row in safe:
                ok, message = kai_call(kai.kill, row["pid"] or row["name"])
                remember("kill", name=row["name"], ok=ok)
                if ok:
                    kip.message(message or "Stopped %s" % row["name"], level="success")


# --------------------------------------------------------------------------- 4. notifications


def notification_lab():
    def chip(chip_id, values):
        level = chip_id.replace("send-", "")
        ok = notify(values.get("message") or "Hello from the showcase", title=values.get("title") or "Showcase",
                    level=level)
        return {"state": "success" if ok else "error", "title": "Notification",
                "text": "Sent a **%s** notification." % level if ok else "The Kai app could not be reached."}

    values = kip.prompt([
        {"name": "title", "type": "text", "label": "Title", "default": "Kai showcase"},
        {"name": "message", "type": "textarea", "label": "Message", "required": True,
         "default": "Hello from Python through the kai module"},
        {"name": "level", "type": "select", "label": "Level", "options": ["info", "warning", "error"], "default": "info"},
        {"name": "count", "type": "number", "label": "How many", "min": 1, "max": 5, "step": 1, "decimals": 0, "default": 1},
        {"name": "delay", "type": "number", "label": "Pause between them (s)", "min": 0, "max": 5, "step": 0.5,
         "decimals": 1, "default": 1},
    ], id="notify-lab", title="Notification lab", back=True, submit_label="Send",
        description="The chips send one right away without leaving this screen.", on_chip=chip,
        chips=[{"id": "send-info", "label": "Test info"}, {"id": "send-warning", "label": "Test warning"},
               {"id": "send-error", "label": "Test error", "danger": True,
                "confirm": {"text": "An error notification looks alarming. Send it?", "confirm_label": "Send"}}])
    count = int(values["count"] or 1)
    for i in range(1, count + 1):
        kip.progress(round(100 * (i - 1) / count), "Sending %d of %d" % (i, count), cancellable=True)
        notify("%s (%d/%d)" % (values["message"], i, count), title=values["title"], level=values["level"])
        time.sleep(float(values["delay"] or 0))
    kip.progress(100, "Done")
    remember("notifications", count=count, level=values["level"])
    kip.message("Sent %d notification(s) at level %s." % (count, values["level"]), level="success")


# --------------------------------------------------------------------------- 5. run another command


def runner():
    ok, names = kai_call(kai.commands)
    if not ok:
        return
    names = [n for n in names if "showcase" not in n.lower()]
    if not names:
        kip.message("There is no other command to run.", level="info")
        return
    values = kip.prompt([
        {"name": "command", "type": "list", "label": "Command", "required": True, "searchable": True,
         "page_size": 8, "options": names},
        {"name": "options", "type": "flags", "label": "Options", "options": [
            {"name": "notify", "label": "Send a tray notification when started", "default": True}]},
    ], id="runner-pick", title="Run another Kai command", back=True, submit_label="Review",
        description="%d command(s) registered in Kai." % len(names))
    name = values["command"]
    if not kip.confirm("Start **%s** in the Kai app?" % name, id="runner-confirm", title="Run it?",
                       confirm_label="Run", cancel_label="No", back=True):
        return
    ok, message = kai_call(kai.run, name)
    if ok:
        remember("run", command=name)
        kip.message(message or "Started %s" % name, level="success")
        if values["options"]["notify"]:
            notify("Started %s from the showcase" % name, title="Runner")


# --------------------------------------------------------------------------- 6. report


def report():
    elapsed = int(time.time() - SESSION["started"])
    lines = ["## Session report", "", "Running for **%dm %02ds**, %d event(s)." % (elapsed // 60, elapsed % 60,
                                                                                   len(SESSION["events"])), ""]
    if SESSION["projects"]:
        lines += ["### Projects", ""] + ["- **%s** (%s, %d files) → `%s`" % (p["name"], p["stack"], p["files"], p["path"])
                                         for p in SESSION["projects"]]
    else:
        lines += ["_No project created yet._"]
    kip.markdown("\n".join(lines))
    if SESSION["events"]:
        kip.table([{"key": "at", "label": "Time"}, {"key": "event", "label": "Event"}, {"key": "info", "label": "Details"}],
                  [{"at": e["at"], "event": e["event"],
                    "info": ", ".join("%s=%s" % (k, v) for k, v in e.items() if k not in ("at", "event"))}
                   for e in SESSION["events"]], id="report-events", title="Events")


# --------------------------------------------------------------------------- main

HANDLERS = {"scaffold": scaffold, "envs": environments, "procs": processes, "notify": notification_lab,
            "runner": runner, "report": report}


def main():
    kip.hello(title="Kai showcase")
    notify("Showcase started", level="info")
    remember("start", pid=os.getpid(), out=str(OUT_BASE))
    OUT_BASE.mkdir(parents=True, exist_ok=True)
    last = "scaffold"
    while True:
        choice = menu(last)
        if choice == "quit":
            break
        last = choice
        try:
            HANDLERS[choice]()
        except kip.Back:
            remember("back-to-menu", tool=choice)  # Back on a tool's first screen
        except Exception as error:  # noqa: BLE001 - a tool must never take the whole session down
            remember("tool-crashed", tool=choice, error=repr(error))
            kip.message("**%s** crashed: `%r`. Back to the menu." % (choice, error), level="error")
    notify("Showcase finished: %d project(s)" % len(SESSION["projects"]), level="info")
    last_project = SESSION["projects"][-1]["path"] if SESSION["projects"] else None
    actions = [{"type": "copy", "label": "Copy session log", "value": json.dumps(SESSION["events"], indent=2)},
               {"type": "open_url", "label": "Kai docs", "url": "https://github.com/"}]
    if last_project:
        actions.insert(0, {"type": "reveal", "label": "Show last project", "path": last_project})
    kip.done(title="See you!", text="%d project(s) created, %d event(s) logged." % (
        len(SESSION["projects"]), len(SESSION["events"])), level="success", actions=actions)


main()

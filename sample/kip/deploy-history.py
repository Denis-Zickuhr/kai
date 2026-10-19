"""Deploy history — KIP demo 23: row actions on a table BLOCK.

The table is a display block (not a field): it has an `id`, so its `row_actions` work while a prompt is open on the
same screen. Logs answers with Markdown in the box under the fields; Roll back asks inside the row first and then
repaints the table (a new `table` with the same id replaces it); rows carry `_actions` so the deploy that is already
rolled back only offers Logs.

Fake data only. Run by Kai as Python code (`language: python`, `kip: true`).
"""
import time

import kip

DEPLOYS = [
    {"id": "d-1042", "version": "1.4.2", "env": {"text": "prod", "tone": "error"}, "status": {"text": "live", "tone": "success"},
     "when": "12 min ago", "by": "Ana"},
    {"id": "d-1041", "version": "1.4.2", "env": {"text": "staging", "tone": "warning"}, "status": {"text": "live", "tone": "success"},
     "when": "1 h ago", "by": "Ana"},
    {"id": "d-1040", "version": "1.4.1", "env": {"text": "prod", "tone": "error"}, "status": {"text": "replaced", "tone": "neutral"},
     "when": "yesterday", "by": "Bruno"},
    {"id": "d-1039", "version": "1.4.0", "env": {"text": "prod", "tone": "error"}, "status": {"text": "rolled back", "tone": "warning"},
     "when": "3 days ago", "by": "Carla", "_actions": ["logs"]},
]


def show_table():
    kip.table(
        [{"key": "id", "label": "Deploy", "format": "copy"}, {"key": "version", "label": "Version", "format": "mono"},
         {"key": "env", "label": "Environment", "format": "badge"}, {"key": "status", "label": "Status", "format": "badge"},
         {"key": "when", "label": "When", "format": "muted"}, {"key": "by", "label": "By", "format": "muted"}],
        DEPLOYS, id="deploys", title="Last deploys", row_key="id",
        row_actions=[
            {"id": "logs", "label": "Logs", "icon": "scroll-text"},
            {"id": "rollback", "label": "Roll back", "icon": "undo-2", "danger": True,
             "confirm": {"text": "Traffic goes back to the previous version.", "confirm_label": "Roll back",
                         "cancel_label": "Keep"}},
        ])


def on_row_action(action, row, values):
    deploy = next(d for d in DEPLOYS if d["id"] == row)
    time.sleep(0.5)
    if action == "logs":
        return {"title": "Logs · " + row, "text": "```\n"
                "12:01:04 pulling registry.local/billing:%s\n12:01:09 3/3 replicas ready\n12:01:10 health check ok\n```"
                % deploy["version"]}
    deploy["status"] = {"text": "rolled back", "tone": "warning"}
    deploy["_actions"] = ["logs"]
    show_table()
    return {"title": "Rolled back · " + row, "text": "Version **%s** is no longer serving traffic." % deploy["version"]}


kip.hello(title="Deploy history (demo)", version="1.0")
show_table()
kip.prompt([], id="history", title="Hover a deploy for its actions", submit_label="Close", remember=False,
           on_row_action=on_row_action)
kip.done(title="Closed")

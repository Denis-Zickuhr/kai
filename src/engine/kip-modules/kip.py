"""kip - Kai Interface Protocol for Python (injected by Kai into KIP commands).

    import kip
    values = kip.prompt([{"name": "env", "type": "select", "options": ["dev", "prod"]}],
                        id="where", title="Where to?")
    kip.progress(50, "Deploying...")
    kip.done(title="Deployed " + values["env"])

`hello` is sent before the first message. Cancel exits the process with code 130;
the Back button raises kip.Back. Full protocol: docs/kip.md.
"""
import json
import sys

VERSION = 1


class Back(Exception):
    """The user pressed Back on the current prompt or confirm."""


_started = False


def _write(msg):
    sys.stdout.write(json.dumps(msg, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def send(kind, **fields):
    """Send any protocol message: kip.send("message", level="info", text="hi")."""
    global _started
    if kind != "hello" and not _started:
        hello()
    if kind == "hello":
        _started = True
    msg = {"kip": VERSION, "type": kind}
    msg.update({k: v for k, v in fields.items() if v is not None})
    _write(msg)


def hello(title=None, version=None):
    send("hello", title=title, version=version)


def log(*parts):
    """Free-form log line (shown in Details > Log)."""
    print(*parts, file=sys.stderr, flush=True)


def _read():
    while True:
        line = sys.stdin.readline()
        if not line:
            sys.exit(130)  # Kai closed our stdin: the run is over
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if not isinstance(msg, dict):
            continue
        if msg.get("type") == "cancel":
            sys.exit(130)
        return msg


def _answer(id, on_change=None, on_chip=None, validate=None, on_row_action=None):
    """Wait for the answer to prompt/confirm `id`, serving change/chip/row action/invalid on the way."""
    while True:
        msg = _read()
        if msg.get("id") != id:
            continue
        kind = msg.get("type")
        if kind == "response":
            values = msg.get("values") or {}
            errors = validate(values) if validate else None
            if errors:
                send("invalid", id=id, errors=errors)
                continue
            return values
        if kind == "back":
            raise Back()
        if kind == "change":
            reply = on_change(msg.get("field"), msg.get("values") or {}) if on_change else None
            patch = reply if isinstance(reply, dict) else {"fields": reply or []}
            send("patch", id=id, seq=msg.get("seq"), **patch)
        elif kind == "chip" and (on_chip or (on_row_action and "row" in msg)):
            chip = msg.get("chip")
            try:
                if "row" in msg and on_row_action:
                    result = on_row_action(chip, msg.get("row"), msg.get("values") or {})
                else:
                    result = on_chip(chip, msg.get("values") or {})
                result = {"text": result} if isinstance(result, str) else (result or {})
                result.setdefault("state", "success")
            except Exception as error:
                result = {"state": "error", "text": str(error)}
            send("chip_result", chip=chip, id=id, **result)


def prompt(fields, id="prompt", on_change=None, on_chip=None, validate=None, on_row_action=None, **options):
    """Show a form and return its values dict.

    options: title, description, submit_label, back, cancellable, remember, chips.
    on_change(field, values) -> fields to patch (a `watch` field changed).
    on_chip(chip, values)    -> text or {state, title, text} for a chip.
    on_row_action(action, row, values) -> the same, for a table row action (`row_actions`).
    validate(values)         -> {field: message} to keep the form open.
    """
    send("prompt", id=id, fields=fields, **options)
    return _answer(id, on_change, on_chip, validate, on_row_action)


def confirm(text, id="confirm", **options):
    """Yes/no screen; returns True/False. options: title, danger, confirm_label, cancel_label, back."""
    send("confirm", id=id, text=text, **options)
    return bool(_answer(id).get("confirmed"))


def message(text, level="info"):
    send("message", level=level, text=text)


def markdown(text):
    send("markdown", text=text)


def progress(value, label=None, **options):
    """value 0-100, or None for an indeterminate bar."""
    if not _started:
        hello()
    msg = {"kip": VERSION, "type": "progress", "value": value}  # null is meaningful here
    msg.update({k: v for k, v in dict(options, label=label).items() if v is not None})
    _write(msg)


def steps(id, items, title=None):
    """items: [{"id": "build", "label": "Build"}, ...]"""
    send("steps", id=id, title=title, items=items)


def step(steps_id, id, state=None, detail=None, log=None):
    """state: pending | running | success | error | skipped (None keeps it); log: lines to append."""
    send("step", steps=steps_id, id=id, state=state, detail=detail, log=log)


def table(columns, rows, id=None, title=None, **options):
    """columns: ["name", ...] or [{"key": "name", "label": "Name", "format": "badge"}, ...].
    options: row_key, row_actions (answered by prompt(on_row_action=...))."""
    columns = [{"key": c, "label": c} if isinstance(c, str) else c for c in columns]
    send("table", id=id, title=title, columns=columns, rows=rows, **options)


def diff(patch=None, left=None, right=None, **options):
    """A diff block: patch="<git diff output>", or left/right as text or {"name", "text"}.
    options: id, title, language, view (unified|split), context, ignore_whitespace, collapse_after."""
    side = lambda s: {"text": s} if isinstance(s, str) else s
    if patch is not None:
        send("diff", patch=patch, **options)
    else:
        send("diff", left=side(left), right=side(right), **options)


def kv(items, id=None, title=None):
    """items: [{"key": "Version", "value": "1.4.0", "format": "mono", "copy": True}, ...]"""
    send("kv", id=id, title=title, items=items)


def stats(items, id=None):
    """items: [{"label": "Tests", "value": "412", "detail": "all passed", "tone": "success"}, ...]"""
    send("stats", id=id, items=items)


def code(text, id=None, title=None, language=None, **options):
    """options: highlight=[4, 5], max_lines, line_numbers."""
    send("code", id=id, title=title, language=language, text=text, **options)


def notify(title, text=None, level=None):
    send("notify", title=title, text=text, level=level)


def set_env(name, value):
    """Export a dynamic variable (the name must be in the command's Exportable variables)."""
    send("set_env", name=name, value=value)


def patch(id, fields=None, remove=None, chips=None):
    """Spontaneous patch of the open prompt `id` (no `seq`): replace fields (by name), remove fields,
    and/or replace the whole chip set. Meant for what the program decides on its own, e.g. a chip
    that repaints a table; answers to `change` are sent by `on_change`."""
    send("patch", id=id, fields=fields if fields is not None else [], remove=remove, chips=chips)


def chip_result(chip, state="success", text=None, title=None, id=None):
    send("chip_result", chip=chip, state=state, text=text, title=title, id=id)


def done(title=None, text=None, level=None, actions=None):
    """Result screen. actions: [{"type": "open_url", "label": "Open", "url": "https://..."}]"""
    send("done", title=title, text=text, level=level, actions=actions)

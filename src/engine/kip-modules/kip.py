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


def _answer(id, on_change=None, on_chip=None, validate=None):
    """Wait for the answer to prompt/confirm `id`, serving change/chip/invalid on the way."""
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
        elif kind == "chip" and on_chip:
            chip = msg.get("chip")
            try:
                result = on_chip(chip, msg.get("values") or {})
                result = {"text": result} if isinstance(result, str) else (result or {})
                result.setdefault("state", "success")
            except Exception as error:
                result = {"state": "error", "text": str(error)}
            send("chip_result", chip=chip, id=id, **result)


def prompt(fields, id="prompt", on_change=None, on_chip=None, validate=None, **options):
    """Show a form and return its values dict.

    options: title, description, submit_label, back, cancellable, remember, chips.
    on_change(field, values) -> fields to patch (a `watch` field changed).
    on_chip(chip, values)    -> text or {state, title, text} for a chip.
    validate(values)         -> {field: message} to keep the form open.
    """
    send("prompt", id=id, fields=fields, **options)
    return _answer(id, on_change, on_chip, validate)


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


def step(steps_id, id, state, detail=None):
    """state: pending | running | success | error | skipped"""
    send("step", steps=steps_id, id=id, state=state, detail=detail)


def table(columns, rows, id=None, title=None):
    """columns: ["name", ...] or [{"key": "name", "label": "Name"}, ...]"""
    columns = [{"key": c, "label": c} if isinstance(c, str) else c for c in columns]
    send("table", id=id, title=title, columns=columns, rows=rows)


def notify(title, text=None, level=None):
    send("notify", title=title, text=text, level=level)


def set_env(name, value):
    """Export a dynamic variable (the name must be in the command's Exportable variables)."""
    send("set_env", name=name, value=value)


def chip_result(chip, state="success", text=None, title=None, id=None):
    send("chip_result", chip=chip, state=state, text=text, title=title, id=id)


def done(title=None, text=None, level=None, actions=None):
    """Result screen. actions: [{"type": "open_url", "label": "Open", "url": "https://..."}]"""
    send("done", title=title, text=text, level=level, actions=actions)

"""A script that runs as a FILE: sample/kip command 18 calls it from a native command with KIP on.

Kai exports PYTHONPATH (and NODE_PATH / PHP_INI_SCAN_DIR for the other languages) pointing at the
modules it keeps on disk, so `import kip` works here without any bootstrap or sys.path hack.
"""
import kip

values = kip.prompt(
    [{"name": "who", "type": "text", "label": "Name", "required": True}],
    id="name",
    title="Who are you?",
)
kip.done(title="Hello, " + values["who"] + "!", text="This is a plain .py file that imported kip from disk.")

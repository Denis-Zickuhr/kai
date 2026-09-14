"""Table browser, DynamoDB style — KIP demo 15.

List tables -> pick one -> browse its items (server-side pages with a cursor, like
LastEvaluatedKey) -> filter -> new / edit / duplicate / delete items.

It uses only what KIP offers today:
  * a `list` field to pick the table;
  * a `table` field for the items, a `watch`ed text field for the filter (change -> patch);
  * chips for Previous / Next / Refresh / Delete (inline confirm) that `patch` the table in place;
  * a second prompt (a JSON editor with `invalid` validation) for New / Edit / Duplicate.

The data is a small in-memory fake so the demo runs anywhere. To use real DynamoDB, replace
`FakeBackend` with a class exposing the same five methods on top of boto3
(list_tables, key_attrs, scan, put, delete).

Run by Kai as Python code (`language: python`, `kip: true`): the `kip` module is injected.
"""
import copy
import json

import kip

PAGE = 6  # items evaluated per page (DynamoDB's Limit)


class FakeBackend:
    def __init__(self):
        self.keys = {"Orders": ["pk"], "Customers": ["pk"], "Events": ["pk", "sk"]}
        self.items = {
            "Orders": [
                {"pk": "o-%03d" % n, "customer": ["Acme", "Globex", "Initech"][n % 3],
                 "status": ["new", "paid", "shipped"][n % 3], "total": 10 + n * 7,
                 "lines": [{"sku": "A-%d" % n, "qty": n % 4 + 1}]}
                for n in range(1, 21)],
            "Customers": [
                {"pk": "c-%d" % n, "name": name, "email": name.lower() + "@example.com", "vip": n % 2 == 0}
                for n, name in enumerate(["Acme", "Globex", "Initech", "Umbrella"], 1)],
            "Events": [
                {"pk": "dev-%d" % (n % 2), "sk": "2024-01-%02d" % n, "kind": "boot" if n % 2 else "crash"}
                for n in range(1, 10)],
        }

    def list_tables(self):
        return sorted(self.items)

    def key_attrs(self, table):
        return self.keys[table]

    def _sort_key(self, table, item):
        return "|".join(str(item[k]) for k in self.keys[table])

    def scan(self, table, cursor, limit, contains):
        """Evaluates `limit` items after `cursor`, THEN filters (as DynamoDB does): a page can hold
        fewer items than `limit`. Returns (matched, next_cursor or None)."""
        ordered = sorted(self.items[table], key=lambda i: self._sort_key(table, i))
        start = 0
        if cursor:
            start = next((n + 1 for n, i in enumerate(ordered) if self._sort_key(table, i) == cursor), len(ordered))
        window = ordered[start:start + limit]
        matched = [i for i in window if not contains or contains.lower() in json.dumps(i).lower()]
        more = start + limit < len(ordered)
        return copy.deepcopy(matched), (self._sort_key(table, window[-1]) if more and window else None)

    def put(self, table, item):
        key = self._sort_key(table, item)
        rest = [i for i in self.items[table] if self._sort_key(table, i) != key]
        self.items[table] = rest + [item]

    def delete(self, table, key):
        self.items[table] = [i for i in self.items[table] if self._sort_key(table, i) != key]


backend = FakeBackend()


class Go(BaseException):
    """Raised by a chip to leave the table screen for another one (not an Exception: the kip
    module turns those into a red chip box, and this one is meant to end the prompt)."""

    def __init__(self, mode, key=None):
        self.mode, self.key = mode, key


def cell(value):
    text = value if isinstance(value, str) else json.dumps(value)
    return text if len(text) <= 48 else text[:45] + "…"


class Browser:
    def __init__(self, table):
        self.table = table
        self.keys = backend.key_attrs(table)
        self.cursors = [None]  # cursor that STARTS each visited page
        self.page = 0
        self.filter = ""
        self.next_cursor = None
        self.items = []
        self.evaluated = 0

    def item_key(self, item):
        return "|".join(str(item[k]) for k in self.keys)

    def load(self):
        self.items, self.next_cursor = backend.scan(self.table, self.cursors[self.page], PAGE, self.filter)

    def columns(self):
        others = []
        for item in self.items:
            for name in item:
                if name not in self.keys and name not in others:
                    others.append(name)
        return [{"key": name, "label": name} for name in self.keys + others[:4]]

    def rows_field(self):
        rows = [dict({"_key": self.item_key(i)}, **{k: cell(v) for k, v in i.items()}) for i in self.items]
        first = self.page * PAGE + 1
        info = "Items %d–%d evaluated · %d shown%s" % (
            first, first + PAGE - 1, len(rows), " · more available" if self.next_cursor else " · end of table")
        return {"name": "rows", "type": "table", "label": self.table, "description": info,
                "row_key": "_key", "multiple": True, "searchable": False,
                "columns": self.columns(), "rows": rows}

    def chips(self):
        chips = []
        if self.page > 0:
            chips.append({"id": "prev", "label": "Previous", "icon": "arrow-left"})
        if self.next_cursor:
            chips.append({"id": "next", "label": "Next", "icon": "arrow-right"})
        chips += [
            {"id": "refresh", "label": "Refresh", "icon": "refresh-cw"},
            {"id": "new", "label": "New item", "icon": "plus"},
            {"id": "edit", "label": "Edit", "icon": "pencil", "requires": ["rows"]},
            {"id": "duplicate", "label": "Duplicate", "icon": "copy", "requires": ["rows"]},
            {"id": "delete", "label": "Delete", "icon": "trash-2", "danger": True, "requires": ["rows"],
             "confirm": {"title": "Delete the selected items?", "text": "This cannot be undone.",
                         "confirm_label": "Delete", "cancel_label": "Keep"}},
        ]
        return chips

    def repaint(self):
        kip.patch("browse", fields=[self.rows_field()], chips=self.chips())

    def on_change(self, field, values):
        if field == "filter":
            self.filter = values.get("filter", "")
            self.cursors, self.page = [None], 0
            self.load()
            return {"fields": [self.rows_field()], "chips": self.chips()}
        return []

    def on_chip(self, chip, values):
        selected = values.get("rows") or []
        if chip == "next":
            self.cursors = self.cursors[: self.page + 1] + [self.next_cursor]
            self.page += 1
        elif chip == "prev":
            self.page -= 1
        elif chip in ("edit", "duplicate"):
            if len(selected) != 1:
                raise ValueError("Select exactly one item.")
            raise Go(chip, selected[0])
        elif chip == "new":
            raise Go("new")
        elif chip == "delete":
            for key in selected:
                backend.delete(self.table, key)
            self.load()
            self.repaint()
            return "Deleted %d item(s)." % len(selected)
        self.load()
        self.repaint()
        return "Page %d · %d item(s) shown" % (self.page + 1, len(self.items))

    def find(self, key):
        for item in backend.items[self.table]:
            if self.item_key(item) == key:
                return copy.deepcopy(item)
        raise ValueError("Item no longer exists.")


def browse(browser):
    browser.load()
    kip.prompt(
        [{"name": "filter", "type": "text", "label": "Filter (text contained in the item)", "watch": True,
          "default": browser.filter, "placeholder": "e.g. shipped", "remember": False},
         browser.rows_field()],
        id="browse", title="Table · " + browser.table, back=True, remember=False,
        submit_label="Close", chips=browser.chips(),
        on_change=browser.on_change, on_chip=browser.on_chip)


def edit(browser, mode, key):
    item = browser.find(key) if key else {k: "" for k in browser.keys}
    if mode == "duplicate":
        item[browser.keys[0]] = str(item[browser.keys[0]]) + "-copy"
    title = {"new": "New item", "edit": "Edit item", "duplicate": "Duplicate item"}[mode]

    def validate(values):
        try:
            parsed = json.loads(values["json"])
        except ValueError as error:
            return {"json": "Not valid JSON: %s" % error}
        if not isinstance(parsed, dict):
            return {"json": "The item must be a JSON object."}
        missing = [k for k in browser.keys if not parsed.get(k)]
        if missing:
            return {"json": "Missing key attribute(s): " + ", ".join(missing)}
        exists = any(browser.item_key(i) == browser.item_key(parsed) for i in backend.items[browser.table])
        if exists and (mode != "edit" or browser.item_key(parsed) != key):
            return {"json": "An item with this key already exists."}
        if mode == "edit" and browser.item_key(parsed) != key:
            return {"json": "The key cannot change when editing (use Duplicate)."}

    values = kip.prompt(
        [{"name": "json", "type": "textarea", "label": "Item (JSON)", "required": True,
          "default": json.dumps(item, indent=2)}],
        id="edit", title=title, back=True, remember=False, submit_label="Save", validate=validate)
    backend.put(browser.table, json.loads(values["json"]))
    kip.message("Saved item " + browser.item_key(json.loads(values["json"])) + ".", level="success")


# The session ends with Cancel (exit 130): the table list is the home screen.
while True:
    picked = kip.prompt(
        [{"name": "table", "type": "list", "label": "Table", "required": True, "searchable": True,
          "options": backend.list_tables()}],
        id="tables", title="DynamoDB tables", submit_label="Open", remember=False)
    browser = Browser(picked["table"])
    while True:
        try:
            browse(browser)  # returns on "Close"
            break
        except kip.Back:
            break
        except Go as go:
            try:
                edit(browser, go.mode, go.key)
            except kip.Back:
                pass  # Back from the editor: nothing saved, show the table again

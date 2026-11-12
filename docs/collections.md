# Collections

A **Collection** is your own table of data (customers, servers, environments — whatever you need) that a [select parameter](parameters.md) can use as its list of options. Create one from **Item → New Collection**; it belongs to a folder/tab.

## Schema and entries

The editor has three tabs on the left, like the command editor:

- **General** — name, icon, folder, order and a summary of the fields; **Schema…** edits them (one is the **key**, one the **value**, plus any others).
- **Data** — the entries in a table. Mark favourites, filter, **import a CSV or JSON file**, or **Paste** records from the clipboard: JSON (an array of objects, or one object) or rows copied from a spreadsheet (tab-separated) or CSV lines. A first row that names the fields (by name or label) is used as the header; otherwise the columns go to the fields in order. Kai shows how many records it found and asks before adding them. Completely empty entries are ignored.
- **Advanced** — the collection as JSON, editable. The text goes back into the collection when you leave the tab or save; invalid JSON keeps you on the tab until it is fixed.

## Example

A command `echo {{cliente.value}}` with a `cliente` parameter of type select bound to the **Clientes** collection (displayed field: `value`). At run time you pick the client and Kai injects `{{cliente.value}}` (the displayed field) *and* `{{cliente.key}}` plus every other field of the entry.

## Versioning with a project

Collections can live inside a project's [kai.yml](kai-yml.md); the parameter references the collection **by name**. When you [export](export-import.md), the entries are left out unless you opt in — they may hold real data.

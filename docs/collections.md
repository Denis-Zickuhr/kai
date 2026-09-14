# Collections

A **Collection** is your own table of data (customers, servers, environments — whatever you need) that a [select parameter](parameters.md) can use as its list of options. Create one from **Item → New Collection**; it belongs to a folder/tab.

## Schema and entries

You define the fields (columns) — one is the **key**, one the **value**, plus any others — and then the entries. You can mark favourites, filter, and **import entries from CSV or JSON** (completely empty entries are ignored).

## Example

A command `echo {{cliente.value}}` with a `cliente` parameter of type select bound to the **Clientes** collection (displayed field: `value`). At run time you pick the client and Kai injects `{{cliente.value}}` (the displayed field) *and* `{{cliente.key}}` plus every other field of the entry.

## Versioning with a project

Collections can live inside a project's [kai.yml](kai-yml.md); the parameter references the collection **by name**. When you [export](export-import.md), the entries are left out unless you opt in — they may hold real data.

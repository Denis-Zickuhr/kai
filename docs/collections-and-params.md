# Collections & Parameters

**Parameters** ask for values before running: `text`, `select`, `bool`,
`file`. A **select** can pull its options from a **Collection**.

## Example

Command `echo {{customer.value}}` with a `customer` parameter of type
`select` tied to the **Customers** collection (displayed field: `value`).

When you run it, you pick the customer and Kai injects:

- `{{customer.value}}` (the displayed field)
- `{{customer.key}}` and every other schema field

## Importing data

Collections accept **CSV/JSON** import; entries that are entirely empty
are ignored.

## Versioning alongside a project

Collections can live inside a project's `kai.json`, and the parameter
references the collection **by name**. See [kai-json.md](kai-json.md).

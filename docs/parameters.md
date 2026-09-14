# Parameters

**Parameters** ask for values in a form *before* a command runs. Add them in the command editor's **Parameters** tab; each one becomes `{{name}}` in the command (or in the URL, headers and body of an HTTP command; in Python/Node code it arrives as an environment variable).

## Types

|   |   |
|---|---|
| **text** | a line of text |
| **textarea** | multi-line text that grows as you type |
| **number** | a numeric field |
| **bool** | a switch (inserted as true/false) |
| **select** | one of a fixed list of options (optionally **multi-select**, joined by commas) *or* an entry of a [collection](collections.md) |
| **file** | a file or folder picker (see below) |
| **json** | a snippet editor with syntax highlighting, handy for bodies |
| **date** | a date picker for a date, a time or both, optionally a range |
| **grid** | a table of rows with one typed field per column (see below) |

## Common options

- **Label** and **default value**. Kai remembers the last values you used and pre-fills them; options of a select are sorted by how recently you used them.
- **Required** puts an asterisk and blocks OK until it is filled.
- **Optional** hides the field behind a checkbox (*Provide \<label>?*) so a long form stays short; it only counts when ticked.
- **Group** — give several parameters the same group name to fold them into a section.
- **Step** — give parameters the same step name to put them on one page of the form. The form then has *Back* / *Next* (and *OK* on the last page); *Next* waits for the page's required fields. Pages follow the order each step first appears, parameters without a step go on the first page, and a group should stay within one step.
- **Description** is shown in the CLI's `--help` (see [CLI](cli.md)).

## File and folder

**Selection mode** picks a file, a folder or *both* (asked at run time). The picker opens in the last folder you used anywhere in Kai. **Path format** converts what the system returns: `native`, `posix` (`/mnt/c/...`) or `windows` (`C:\...`) — useful when a Windows Kai runs commands in WSL.

## Date

**Date field shows** date, time or date & time. With **Range**, `{{name}}` is the start and `{{name.end}}` the end. **Paste format** chooses how the value is written: ISO, BR (15/01/2024), US, 24h time, Unix seconds or milliseconds, or a custom template (`yyyy-MM-dd HH:mm`).

## Grid

A **grid** collects *N rows* of several fields each — say the new price of many products. In the parameter editor add the **columns** (name, label and a type: text, number, bool, select or date) and, optionally, a **row template**. In the run form each row is a line of typed fields; **Add row** appends one and the clipboard button pastes rows copied from a spreadsheet (or CSV), one record per line, columns in order.

| Injected | Value |
|---|---|
| `{{name}}` | with no template, the JSON array of the rows: `[{"sku":"A-1","price":19.9}]` (numbers and bools typed, keys in column order) |
| `{{name}}` | with a **row template**, each row written through it and joined by the **separator** (`,` by default; `\n` and `\t` mean newline and tab) |
| `{{name__json}}` | always the JSON array |
| `{{name__count}}` | how many rows |

In the template `{{column}}` is the cell text and `{{column|json}}` its JSON literal (text quoted and escaped, number and bool bare), e.g. `{"sku":{{sku|json}},"price":{{price|json}}}`; anything else in `{{ }}` is left for the usual variables. A body like `{"products": {{products}}}` then just works. The rows you filled are remembered for the next run. On the command line pass the JSON array (`kai … '[{"sku":"A-1","price":2}]'`); columns you leave out take their default.

## Select tied to a collection

Choosing an entry injects the displayed field as `{{name}}` *and* every field of the entry as `{{name.field}}` — see [Collections](collections.md).

> **Tip:** On the command line the required parameters are positional and the optional ones are `--name=value` — see [CLI](cli.md).

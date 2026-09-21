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

## Common options

- **Label** and **default value**. Kai remembers the last values you used and pre-fills them; options of a select are sorted by how recently you used them.
- **Required** puts an asterisk and blocks OK until it is filled.
- **Optional** hides the field behind a checkbox (*Provide \<label>?*) so a long form stays short; it only counts when ticked.
- **Group** — give several parameters the same group name to fold them into a section.
- **Description** is shown in the CLI's `--help` (see [CLI](cli.md)).

## File and folder

**Selection mode** picks a file, a folder or *both* (asked at run time). **Initial folder** is where the picker opens. **Path format** converts what the system returns: `native`, `posix` (`/mnt/c/...`) or `windows` (`C:\...`) — useful when a Windows Kai runs commands in WSL.

## Date

**Date field shows** date, time or date & time. With **Range**, `{{name}}` is the start and `{{name.end}}` the end. **Paste format** chooses how the value is written: ISO, BR (15/01/2024), US, 24h time, Unix seconds or milliseconds, or a custom template (`yyyy-MM-dd HH:mm`).

## Select tied to a collection

Choosing an entry injects the displayed field as `{{name}}` *and* every field of the entry as `{{name.field}}` — see [Collections](collections.md).

> **Tip:** On the command line the required parameters are positional and the optional ones are `--name=value` — see [CLI](cli.md).

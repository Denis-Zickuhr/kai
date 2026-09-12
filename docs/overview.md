# Overview

**Kai** is a native command runner (C++/Qt6) for developers: it runs Shell
scripts and HTTP requests, chains automations (hooks), injects
environment-scoped variables — all behind a global shortcut and with a
light footprint. The UX inspiration is **CopyQ**: light, native, with a
global shortcut.

## Typical flow

1. Create **folders** and, inside them, **commands** (Shell or HTTP).
2. Set **variables** in an **Environment** (a selectable package at the top).
3. Run with a double-click; watch the output in the output panel.
4. Automate with **hooks** (pre/post/cleanup) and reuse via the **CLI**.

## Concepts

- **Folder:** groups commands; becomes a tab. Can have subfolders.
- **Command:** an executable unit (Shell or HTTP).
- **Environment:** a package of variables; one active at a time.
- **Hook:** a command fired before/after/on cleanup of another.
- **Execution condition:** a guard deciding whether a command actually runs.
- **Collection:** a data source for `select` parameters.

See also: [Shell commands](commands-shell.md), [HTTP commands](commands-http.md),
[Environments](environments.md).

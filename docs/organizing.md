# Folders, tree & search

## Folders and commands

Commands live inside **folders**. Each top-level folder becomes a **tab**; subfolders nest inside it. Create things from the **Item** menu, the side bar or the right-click menu: **New Folder**, **New Command**, **New Collection** (see [Collections](collections.md)).

A folder can carry its own settings: name, icon, parent folder, the [execution profile](terminal-targets-wsl.md) its commands inherit, its own [variables](variables.md) and a [CLI path](cli.md). A project imported from a [kai.json](kai-json.md) becomes a folder whose path is available as `{{PROJECT_PATH}}`.

## The right-click menu

On a command: **Edit**, **Duplicate**, **Delete**. On a folder: the same, plus **Hide / Show Folder**. The same menu opens from the keyboard (**Ins** by default). **Run**, **Stop** (terminate), **Force stop** and **Reset and restart** are in the action side bar.

## Search

**Ctrl+F** shows the search bar; it filters commands by name with a fuzzy match (typos and gaps are fine). Press it again to hide.

## Order, hiding and filtering

- **Order:** the command editor has an **Order** field. Lower numbers come first; *automatic* sorts by name.
- **Hide:** hidden items leave the tree until **Show hidden items** is on. Hide a command from its editor or a folder from the menu.
- **Show only running commands:** a quick filter for what is running right now.
- **Expand / collapse** the selected folder or everything.

## Editing

- Every editor has a **Simple** and an **Advanced (JSON)** mode; **Ctrl+E** switches an open editor.
- **Edit body (quick)** in the side bar opens just the body of an HTTP command.
- Icons come from the built-in set or any image file you pick.
- **Duplicate** copies a command, a folder or a collection.

> **Tip:** Every action here has a keyboard shortcut you can change — see [Keyboard shortcuts](shortcuts.md). **Settings → Storage** lists, duplicates and deletes many items at once.

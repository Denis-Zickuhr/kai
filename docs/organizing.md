# Folders, tree & search

## Folders and commands

Commands live inside **folders**. Each top-level folder becomes a **tab**; subfolders nest inside it. Create things from the **Item** menu, the side bar or the right-click menu: **New Folder**, **New Command**, **New Collection** (see [Collections](collections.md)).

A folder can carry its own settings: name, icon, parent folder, the [execution profile](terminal-targets-wsl.md) its commands inherit, its own [variables](variables.md) and a [CLI path](cli.md). A folder also has a **working directory**, inherited by its subfolders and commands: each one can inherit it, set its own (a relative path is resolved over the inherited one) or choose *None* to cut the inheritance. A project imported from a [kai.yml](kai-yml.md) becomes a project folder whose working directory is the project root; `{{PROJECT_PATH}}` is always the root of the nearest project folder.

## Syncing a project folder with its file

A project folder can be kept in step with a `kai.yml` in its working directory, **by hand**: right-click the folder and choose **Sync Kai → File** or **Sync File → Kai**. Nothing runs on its own.

- **Kai → File** writes the folder's subfolders and commands to the file — by **path and name, with no ids** (a command's identity is its folder path + its name: loading the file keeps the id, remembered values and links of whatever stays in place with the same name; renaming one makes it a new item) — plus the folder's own variables (a variable marked **Secret** is written by name only: its value stays in your Kai, and the file never overwrites it) and **folder actions** (with their expansion flag and groups). **Collections are never written**: a select parameter carries only the collection's **name** as a reference (collections can be big or personal), and global actions stay in Settings, out of the file. An action that points to a command outside the project is written by that command's **name**. Remembered parameter values and history never go either. Multi-line text (a script in `command`) is written as a readable `|` block. The file is always `kai.yml` (a `kai.json` is neither read nor touched).
- **File → Kai** replaces everything *inside* the folder with the file's content (the folder itself stays).
- Kai remembers what both sides looked like after each sync. If the side that would be **overwritten** changed since then — or was never synced — you're asked to confirm first, with the counts of what would be replaced.
- **File → Kai** reads both a hand-written project file (the format described in [kai.yml](kai-yml.md): `folder` paths, hooks by name, no ids) and the format **Kai → File** writes. It never modifies the file. Collection **references** are matched by name with the collections you have (only when the name is unique); the ones that aren't found are reported and the parameter is left without a source. Collections defined inside a file are not imported by the sync (they are reported as skipped); commands that already exist keep the parameter values Kai remembered. Actions by name are matched with your commands the same way.
- **Kai → File** always writes Kai's lean export format, so over a hand-written file it asks first and replaces it.
- **Out-of-sync mark:** a project folder that has been synced before gets an **amber dot** on its icon (and a tooltip saying what changed) when Kai changed and the file doesn't have it yet, when the file changed (or disappeared) and Kai doesn't have it, or both. Kai checks when it opens, when the window regains focus, after you save changes and after a sync — it doesn't watch the file. Running a command (remembered parameter values) doesn't count as a change; renaming a collection that a parameter points to does (the reference changes), editing the collection's data doesn't. A folder that was never synced isn't marked.

## Folder actions

An **action** is an existing command mapped to appear as an icon on a folder's row — handy for things like `git fetch`, `pull` or `status`, which you want to run *on this folder* without copying the command everywhere.

- **Per folder:** edit the folder → **Actions** tab. It works like the dynamic parameters: **Add action** and the count sit outside the table, the table lists just the command names (drag to reorder, pencil to edit, trash to remove) and a small form picks the command with the usual searchable selector. They show on the row **left to right, in the order you registered them**, at the **end of the row, after the time marker**.
- **For every folder:** **Settings → Actions**. Each global action can be limited to **project folders** only. On a row, the global ones come first, then the folder's own; a command listed in both shows once.
- Actions appear on **subfolders**. A top-level folder is a tab, with no row, so it doesn't show them.
- **Expansion actions:** an action's form has an **Expansion action** switch (per-folder and global actions alike) for the useful-but-not-so-much ones. They get no icon on the row; they stay hidden behind a single **expand symbol** (chevron) after the icons, and clicking it opens a menu with them. They behave exactly like the others — click runs / loads / shows the output, right-click offers the same menu, the item's icon is green while it runs, and the chevron turns green when any hidden action runs. Export/import carries the flag with the action.
- **Groups:** type a **Group** in an action's form (a theme such as "Git" or "Docker") and optionally pick a **group icon** (default when empty). Actions of the same group share **one icon** on the row that opens a menu with them (same behavior as the others: click runs / loads / shows the output, right-click offers the same menu, green while any runs). A group is one across global and folder actions (case-insensitive name); its icon — the first one informed — applies to all of them. A grouped action doesn't need the expansion flag. Row order: plain icons, one icon per group, then the expand symbol. Export/import carries the group (and its icon) with the action.
- **Click an icon** — what it does depends on the icon: if the action **never ran** it runs it; if it **already ran** (its output is kept) and isn't loaded, the click **loads** that output without running; and with the output **loaded** (the icon is focused), hovering shows a **play** glyph and the click **runs it again**. Whenever the click will only *show* an output — the action is running, or it ran and isn't loaded yet — hovering shows an **eye** instead, so it is clear nothing will execute. While it's running a click just brings the output up. Running uses that folder as context: the command's working directory becomes the folder's (inherited like any working dir; with none, the target's default), and the folder's variables, terminal profile and `{{PROJECT_PATH}}` apply. The command's own working directory, cron schedule, auto-run and CLI path don't apply to an action run. A command with parameters asks for them as usual. A command using the KIP interface works as an action too: its KIP view shows in the output panel, and remembered answers are saved on the command.
- **State:** the icon is **green while that action runs on that folder** and the accent color when idle. The same action can run on several folders **at the same time**, each with its own process and output.
- **Output:** the run's output opens in the output panel and the icon gets **focused**. It is kept in memory per action and folder (until Kai closes), so you can leave and come back to it. Click anywhere else on the same folder row (or select another row) to unfocus; the saved output stays.
- **Right-click** an icon for *Run*, *Show output*, *Stop*, *Force stop* and *Edit command*.
- **Export / import:** a folder export carries its actions **by command name**; **Global actions** is its own item in the global export/import. On import, names are matched with the commands that exist — only when the name is unique; the ones that can't be matched are reported. When you export a folder whose actions use commands that live **outside** it, **Folder actions** (on by default) also carries those commands. Whoever imports sees *Commands used by folder actions*: the ones they don't have yet go into an **Imported actions** folder and the actions get linked to them; a command they already have with the same name is reused, not copied.

## Folder documentation

Select a folder and the **Output** area shows its documentation: the **README.md** in the folder's working directory opens by itself, with nothing to configure. Without a README, the first document found there opens instead; in a folder with no documents at all, a button in the middle creates the **README.md**. The tree of files and notes starts collapsed — open it with the first button on the top bar.

- **Links:** a relative link to another `.md` opens it right there; the bar on top has **back / forward**, the clickable **path** and a button for the **tree of the folder's documents**. The **zoom** stays on that bar.
- **Kai links** redirect to Kai itself: `[Start](kai:run/Dev server)` runs a command (after asking you), `kai:open/Name` selects a command or folder and `kai:env/Name` switches the environment (also after asking). A name with spaces works as is.
- **Diagrams:** a ` ```mermaid ` block with a `graph TD` / `flowchart LR` is drawn as a diagram; other diagram types stay as code.

## Files, editing and notes in the documentation

- **File explorer:** the side panel opens by itself when the folder has other files. Search by name; the funnel chooses the file types shown (default `.md`, `.txt` and files without extension — the menu lists the extensions found); the file-plus button creates a new file in the selected folder (no overwrite).
- **Editing:** the **pencil** next to the magnifier turns the viewer into an editor — syntax colors, line numbers, auto-indent and live validation for JSON, YAML and XML. **Save** (Ctrl+S) and **Discard** sit next to it; leaving a file with unsaved changes asks first. Files over 1 MB, binaries and files with extremely long lines are view-only.
- **Editor shortcuts (VS Code style):** while editing —
  - *Lines:* **Alt+↑/↓** move the line(s), **Shift+Alt+↑/↓** copy them, **Ctrl+Shift+K** delete, **Ctrl+Enter** / **Ctrl+Shift+Enter** new line below / above, **Ctrl+L** select line (again: next line), **Ctrl+C / Ctrl+X** without a selection copy / cut the whole line, **Home** goes to the first non-blank character first.
  - *Multiple cursors:* **Ctrl+D** selects the word, then the next occurrence; **Ctrl+Shift+L** selects every occurrence; **Ctrl+Alt+↑/↓** adds a cursor above / below; **Alt+click** adds a cursor; **Shift+Alt+drag** selects a column; **Esc** goes back to one cursor. Typing, Backspace/Delete, Enter, Tab, arrows, copy/cut/paste act on all cursors (one undo step).
  - *Code:* **Ctrl+/** comments the line(s) (YAML `#`; XML and Markdown `<!-- -->`; JSON and plain text have none), **Ctrl+[ / Ctrl+]** outdent / indent, **Tab / Shift+Tab**, **Ctrl+Shift+\\** jumps to the matching bracket (the pair is highlighted), **Ctrl+G** goes to a line, **Shift+Alt+F** formats the document.
  - *Typing:* `()`, `[]`, `{}` and quotes close themselves (typing the closer skips over it, Backspace between an empty pair deletes both, an opener over a selection surrounds it); in XML, `>` writes the closing tag and Enter between tags opens the block.
  - *Search:* **Ctrl+F** searches; **Ctrl+H** adds a replace row (replace one / replace all).
- **Tools** (wand): format/minify/sort keys/escape/validate JSON; pretty/minify/validate XML; convert JSON ⇄ YAML and JSON ⇄ XML; Base64 and URL encode/decode; sort lines, remove duplicates, upper/lower/title case, trim trailing spaces. They act on the selection, or on the whole text, in one undo step.
- **Notes:** *New Note* (right-click menu) creates quick text kept inside Kai (Markdown, text, JSON, YAML or XML), shown in the tree with a notebook icon and in a **Notes** section below the files of the folder — even when the folder is not a project. A note is **local** by default (stays in this Kai, never exported); turn *Only in this Kai* off in *Note Properties* and it travels with exports and with the project file (`notes:` in `kai.yml`, see the manifesto §8.1).
- **Hidden commands** run by schedule, auto-run or `kai run` run quietly: they do not open the output or create a tab.

## The right-click menu

On a command: **Edit**, **Duplicate**, **Export…**, **Delete**. On a folder: the same, plus **Hide / Show Folder**. **Export…** opens the export dialog already pointing at that item. On a **project folder** (one with a working directory) there are also the two sync actions below. The same menu opens from the keyboard (**Ins** by default). **Run**, **Stop** (terminate), **Force stop** and **Reset and restart** are in the action side bar.

## Search

**Ctrl+F** shows the search bar; it filters commands by name with a fuzzy match (typos and gaps are fine). Press it again to hide. Searching opens the folders that hold a match; when you stop searching, every folder goes back to **how it was** before (no tree left wide open).

## Order, hiding and filtering

- **Order:** the command editor has an **Order** field. Lower numbers come first; *automatic* sorts by name.
- **Hide:** hidden items leave the tree until **Show hidden items** is on. Hide a command from its editor or a folder from the menu. Editing a hidden command or folder keeps it hidden.
- **Show only running commands:** a quick filter for what is running right now. Root folders (tabs) with nothing running **disappear** while it is on, and folders go back as they were when you turn it off.
- **Expand / collapse** the selected folder or everything.

## Editing

- Every editor has a **Simple** and an **Advanced (JSON)** mode; **Ctrl+E** switches an open editor.
- **Edit body (quick)** in the side bar opens just the body of an HTTP command.
- Icons come from the built-in set or any image file you pick.
- **Duplicate** copies a command, a folder or a collection.

> **Tip:** Every action here has a keyboard shortcut you can change — see [Keyboard shortcuts](shortcuts.md). **Settings → Storage** lists, duplicates and deletes many items at once.

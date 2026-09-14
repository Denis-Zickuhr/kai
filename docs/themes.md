# Themes

Pick your theme in **Settings**. Themes are JSON/QSS files under
`~/.config/kai/themes/` with **live reload** — editing the file reflects
immediately, no restart needed.

## Structure (summary)

A theme defines color variables (e.g. `accent_color`, background, text)
and a QSS block. The sidebar's neutral icons are recolored with the
theme's accent color.

## Creating a theme

1. Copy an existing theme (e.g. `dracula.json`).
2. Adjust the colors/QSS.
3. Select it in Settings (or edit the active one and watch the live
   reload).

## Themes made by the AI

With the [AI assistant or MCP bridge](ai.md) you can ask for a theme
("a dark ocean theme with a cyan accent"). The assistant writes it to your
themes folder after you approve, and it appears in Settings like any other.
Built-in themes are never overwritten.

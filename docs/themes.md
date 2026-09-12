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

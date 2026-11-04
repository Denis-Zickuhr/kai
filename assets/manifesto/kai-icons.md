<!--
Generated from assets/icons/icons.qrc (source of truth) plus the alias pool in
src/ui/shared/icon-picker-widget.cpp. When an icon is added to the app, regenerate this list
from those two sources. tests/test_manifestos.cpp checks that every name below really exists.
-->

# Valid Kai icons

Every `icon:` of a folder, command or collection in a `kai.yml` accepts one of the names below.
A name that is not in the list does not break the import — Kai just shows the default icon — but
to generate a correct file the first time, use only these values.

All names are [Lucide](https://lucide.dev) icons: exactly the Lucide name (kebab-case), without
`.svg` and without any prefix. Write it as a double-quoted string: `icon: "rocket"`.

## Friendly aliases (optional)

These shortcuts exist only for convenience/backwards compatibility; each points to a Lucide icon of
the main list. Prefer the Lucide name.

| Alias | Equivalent Lucide icon |
|---|---|
| `gear` | `settings` |
| `browser` | `globe` |
| `trash` | `trash-2` |
| `warning` | `triangle-alert` |
| `home` | `house` |
| `api` | `webhook` |
| `build` | `hammer` |
| `test` | `flask-conical` |
| `folder`, `terminal`, `network`, `file`, `play`, `database`, `rocket` | (same Lucide name) |

## Full list (bundled Lucide icons)

`activity`, `alarm-clock`, `archive`, `archive-restore`, `arrow-down`, `arrow-left`,
`arrow-right`, `arrow-up`, `at-sign`, `axe`, `battery`, `bell`,
`bell-off`, `bell-ring`, `binary`, `blocks`, `bookmark`, `bookmark-check`,
`bookmark-x`, `box`, `boxes`, `braces`, `bug`, `bug-off`,
`bug-play`, `calendar`, `calendar-check`, `calendar-clock`, `calendar-days`, `check`,
`chevron-down`, `chevron-left`, `chevron-right`, `chevron-up`,
`circle-alert`, `circle-check`, `circle-help`, `circle-pause`, `circle-play`,
`circle-stop`, `circle-user`, `circle-x`, `clipboard`, `clipboard-check`, `clipboard-copy`,
`clipboard-list`, `clipboard-paste`, `clipboard-type`, `clipboard-x`, `clock`, `cloud`,
`cloud-download`, `cloud-off`, `cloud-rain`, `cloud-upload`, `cloudy`, `code`,
`code-xml`, `cog`, `command`, `compass`, `component`, `contact`,
`container`, `copy`, `copy-check`, `copy-x`, `cpu`, `database`,
`database-backup`, `database-zap`, `dollar-sign`, `dot`, `download`, `drill`,
`droplet`, `euro`, `expand`, `external-link`, `eye`, `eye-off`,
`fast-forward`, `file`, `file-check`, `file-code`, `file-cog`, `file-diff`,
`file-key`, `file-lock`, `file-plus`, `file-search`, `file-text`, `file-x`,
`files`, `flag`, `flag-triangle-right`, `flame`, `flask-conical`, `folder`,
`folder-check`, `folder-cog`, `folder-git-2`, `folder-input`, `folder-open`, `folder-output`,
`folder-pen`, `folder-plus`, `folder-search`, `folder-tree`, `folder-x`, `funnel`,
`gauge`, `git-branch`, `git-commit-horizontal`, `git-fork`, `git-merge`, `git-pull-request`,
`globe`, `globe-lock`, `grip`, `grip-horizontal`, `grip-vertical`, `hammer`,
`hand`, `hard-drive`, `hard-drive-download`, `hash`, `heart`, `heart-off`,
`house`, `inbox`, `info`, `key`, `key-round`, `key-square`,
`keyboard`, `languages`, `laptop`, `layers`, `layout-grid`, `layout-list`,
`leaf`, `link`, `link-2`, `list`, `list-checks`, `list-ordered`,
`list-plus`, `list-tree`, `list-x`, `loader`, `loader-circle`, `lock`,
`lock-keyhole`, `lock-open`, `log-out`, `mail`, `mail-open`, `mails`,
`map`, `map-pin`, `maximize`, `maximize-2`, `message-circle`, `message-square`,
`minimize`, `minimize-2`, `minus`, `monitor`, `moon`, `mouse-pointer`,
`mouse-pointer-click`, `move`, `move-horizontal`, `move-vertical`, `navigation`, `network`,
`notebook`, `notebook-pen`, `octagon-x`, `package`, `package-check`, `package-plus`,
`package-search`, `package-x`, `panel-bottom`, `panel-bottom-close`, `panel-bottom-open`, `panel-left`,
`panel-right`, `panel-top`, `pause`, `pen`, `pen-tool`, `pencil`,
`pencil-line`, `pencil-ruler`, `percent`, `pickaxe`, `pin`, `pin-off`,
`play`, `plug`, `plug-zap`, `plus`, `pointer`, `power`,
`power-off`, `puzzle`, `qr-code`, `radio`, `radio-tower`, `recycle`,
`redo`, `redo-2`, `refresh-ccw`, `refresh-cw`, `repeat`, `repeat-1`,
`rewind`, `rocket`, `rotate-3d`, `rotate-ccw`, `rotate-cw`, `rss`,
`satellite-dish`, `save`, `save-all`, `scan`, `scan-line`, `scissors`,
`scroll-text`, `search`, `send`, `send-horizontal`, `server`, `server-cog`,
`server-crash`, `settings`, `settings-2`, `share`, `share-2`, `shield`,
`shield-alert`, `shield-check`, `shield-x`, `shrink`, `shuffle`, `signal`,
`skip-back`, `skip-forward`, `sliders-horizontal`, `sliders-vertical`, `smartphone`, `snowflake`,
`sparkles`, `square`, `square-activity`, `square-code`, `square-plus`, `square-terminal`,
`star`, `star-off`, `sun`, `sunrise`, `sunset`,
`table`, `table-2`, `tablet`, `tag`, `tags`, `terminal`,
`thumbs-down`, `thumbs-up`, `timer`, `timer-off`, `toggle-left`, `toggle-right`,
`trash-2`, `trees`, `trending-down`, `trending-up`, `triangle-alert`,
`undo`, `undo-2`, `unlink`, `upload`, `user`, `user-check`,
`user-cog`, `user-plus`, `user-x`, `variable`, `wand-sparkles`, `webhook`,
`webhook-off`, `wifi`, `wifi-off`, `wind`, `wrench`, `x`,
`zap`, `zap-off`

> Some names in the `.qrc` (`check-on`, `check-on-dark`, `chevron-up-fg`, `star-filled`) are internal
> parts of other controls (switches, favorite stars) and are left out — do not use them as an icon.

## Custom icon (local file)

The app's icon picker also accepts a local file path prefixed with `file:` (e.g.
`icon: "file:/home/user/logo.png"`). That is a UI convenience — **not recommended** in a `kai.yml`
that will be shared or versioned, because the path only exists on one machine. For files meant to
be imported by other people (such as one generated by an AI agent), always use a name from the list.

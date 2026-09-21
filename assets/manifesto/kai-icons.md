<!--
Gerado a partir de assets/icons/icons.qrc (fonte de verdade) + do pool de
aliases em src/ui/icon-picker-widget.cpp. Se um ícone novo for adicionado ao
app, regenere esta lista a partir dessas duas fontes — não edite os nomes
"à mão" sem conferir contra o .qrc.
-->

# Ícones válidos do Kai

Todo campo `"icon"` de `folder`, `command`, `collection` ou terminal profile
(no `kai.json`/`kai.yml` de projeto e no Export/Import Configuration) aceita
uma destas strings. Um nome fora desta lista não quebra o import — o app
simplesmente não encontra um ícone e mostra o fallback padrão (pasta/arquivo
genérico) — mas para gerar um arquivo correto de primeira, use só os valores
abaixo.

Todos os nomes são do conjunto [Lucide](https://lucide.dev) — o nome aqui é
exatamente o nome do ícone Lucide (kebab-case), sem `.svg` e sem prefixo.

## Aliases amigáveis (opcionais)

Estes atalhos existem só por conveniência/retrocompatibilidade — cada um
aponta para um ícone Lucide da lista principal abaixo. Prefira o nome Lucide
direto ao gerar um kai.json novo; os aliases seguem funcionando mas não
adicionam nenhum ícone que a lista abaixo já não tenha.

| Alias | Ícone Lucide equivalente |
|---|---|
| `gear` | `settings` |
| `browser` | `globe` |
| `trash` | `trash-2` |
| `warning` | `triangle-alert` |
| `home` | `house` |
| `api` | `webhook` |
| `build` | `hammer` |
| `test` | `flask-conical` |
| `folder`, `terminal`, `network`, `file`, `play`, `database`, `rocket` | (mesmo nome Lucide) |

## Lista completa (ícones Lucide embarcados)

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

> Alguns nomes do `.qrc` (`check-on`, `check-on-dark`, `chevron-up-fg`,
> `star-filled`) são partes internas de outros controles do app (switches,
> estrelas de favorito) e foram omitidos desta lista — não use como ícone de
> `folder`/`command`, mesmo que tecnicamente existam no pacote de recursos.

## Ícone customizado (arquivo local)

Além dos nomes acima, o seletor de ícones do app também aceita um caminho de
arquivo local prefixado com `file:` (ex: `"icon": "file:/home/user/logo.png"`)
para usar uma imagem própria. Isso é uma conveniência de UI — **não é
recomendado** para um `kai.json`/`kai.yml` que será compartilhado ou versionado
com outras pessoas, já que o caminho só existe na máquina de quem criou o
arquivo. Para arquivos pensados para import por terceiros (o caso de uso de um
agente de IA gerando um kai file), use sempre um dos nomes Lucide desta lista.

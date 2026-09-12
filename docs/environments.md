# Environments (variable packages)

Global variables live in **packages** (Insomnia/Postman vibe). Only one is
**active** at a time, and it feeds the Global scope.

## Switching packages

- **Picker at the top-right** of the window.
- From the CLI: `kai env use Prod` (see [cli.md](cli.md)).

## Managing them

The ⚙ button next to the picker opens the screen to **create / duplicate /
rename / delete** packages and edit their variables.

## Secret variables

Check the **Secret** column to mask the value in the UI (useful for
tokens/passwords). The value is still used for interpolation.

> Migration: an old `settings.json` (with only `global_env_vars`)
> automatically becomes an active **"Global"** package — nothing is lost.

## Persistence

`~/.config/kai/settings.json` → the `environments` and
`active_environment_id` keys.

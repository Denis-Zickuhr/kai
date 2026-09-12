# Core Development Guidelines (Agent Guidelines)

You are a software engineer specialized in C++20 and Qt 6, responsible for
building **Kai**. Read the files under `specs/` carefully before
implementing any feature.

## 1. Non-negotiable code principles
- **C++20 architecture:** use modern features (`std::optional`,
  `std::variant`, concepts, smart pointers) and avoid manual memory
  management (explicit `new`/`delete` outside the `QObject` tree).
- **Never block the main (GUI) thread:** file I/O, external process calls
  (`QProcess`) and HTTP requests (`QNetworkAccessManager`) **must** be
  100% asynchronous.
- **Decoupling via Qt signals & slots:** business logic (`core/`,
  `engine/`) must not depend directly on UI classes (`ui/`).
  Communication between modules and the UI goes through signals and slots
  (`QObject::connect`).
- **Naming conventions:**
  - Classes/structs: `PascalCase` (e.g. `EnvironmentManager`,
    `ProcessRunner`).
  - Methods/variables: `camelCase` (e.g. `parseConfig()`,
    `activeFolderPath`).
  - Private class members: `m_` prefix (e.g. `m_process`).
  - File names: `kebab-case` matching the class (e.g.
    `environment-manager.cpp`).

## 2. Workflow
1. Work feature by feature, one at a time.
2. Before considering something done, the code must compile without
   warnings (`-Wall -Wextra`).
3. Write the test alongside the change (Qt Test), not after. For a bug
   fix, the test must **fail** against the old code — verify this,
   otherwise it protects nothing.

## 3. Scope & error handling
- Follow the exception-handling and corrupted-JSON-recovery rules in
  `specs/07-error-handling-and-edge-cases.md`.
- If an environment variable isn't found in the hierarchy (`Global ->
  Folder -> Dynamic -> Parameters`), never crash or throw an unhandled
  exception: return a safe fallback (empty string) and log it.

## 4. Expected file structure
- `src/core/`: settings, environments, and data model logic.
- `src/engine/`: subprocess execution, HTTP client, hooks.
- `src/ui/`: Qt Widgets components.
- `src/utils/`: theme manager, file parsers, and helpers.

## 5. Build, install & binary generation
- After finishing a feature or fix, make sure the project builds by
  running `./build.sh`.
- The compiled binary should land in `build/bin/kai`.
- `./build.sh install` should install the final executable to
  `~/.local/bin/kai`, making it immediately available on the user's
  system.

## 6. Additional rules
- Comments only where they're genuinely useful.
- Keep a `sample/` folder with projects for manually exercising the app.

## 7. Delivery & manual validation (REQUIRED)
When wrapping up any development cycle (a feature, a bug fix, or a batch
of adjustments), always hand the user a **manual test checklist** — a
concrete list of `[ ]` boxes describing what to verify in the UI to
confirm the work. It should:
- Group items by the area/feature touched.
- Be written as concrete, observable steps ("clicking X opens Y", "field
  Z only appears when the type is W"), not internal technical terms.
- Call out fixed bugs and how to reproduce the scenario that used to fail.
- Flag any automated test failure that's environmental (e.g. a running
  app instance affecting `test_cli_client`), separating it from real
  regressions.
- Report the build state (does it compile? do the tests pass? is the
  binary installed?).

This applies to all future work, without needing to be asked again.

## 8. Internationalization (i18n) — REQUIRED
No user-facing string may be hardcoded in a single language in the source.
Every piece of UI text (dialog titles, labels, buttons, tooltips,
placeholders, message boxes, status badges, column headers, CLI help text
in `src/ipc/`) must go through `utils::tr(QStringLiteral("key.i18n"))`,
with the key present in **both** `assets/i18n/en.json` and
`assets/i18n/pt.json` (a missing or orphaned key breaks
`tests/test_i18n_sync.cpp` — always run it after adding new text). Pick
stable, specific keys (e.g. `"settings.shortcut.terminal_mode"`), never a
generic one reused out of context. What is **not** i18n (don't confuse
these): internal names used as combo `data` (e.g. `"upper"`,
`"remember"`), JSON/settings keys, object names (`setObjectName`),
regexes, paths, icon names (`LucideIcons::icon`), and
`utils::Logger::*` lines (diagnostic logging, not UI — but don't decide
this alone if the same line also surfaces in some screen of the app).

## 9. Corner style / border radius — REQUIRED
Any element drawing a `border-radius` (boxes, fields, chips, badges,
pills, buttons, popups, overlays) MUST use the design system's radius
tokens (`utils::tokens::radiusSm/radiusMd/radiusLg`), which derive from
the user's corner preference (Settings → Appearance → Corners:
sharp/soft/rounded). NEVER hardcode a pixel radius (`border-radius: 8px`),
and never omit `border-radius` when overriding a `setStyleSheet` locally —
overriding a `QLineEdit`/`QComboBox`/etc.'s global rule needs to redeclare
the radius with the token, otherwise the element goes square regardless of
the user's setting. Exception: perfect circles (e.g. a status dot), where
the radius is half the side by geometric definition.

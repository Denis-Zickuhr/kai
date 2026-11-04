# Contributing to Kai

Thanks for helping. Read [`docs/philosophy.md`](docs/philosophy.md) first: it
says what Kai is and is not, and settles most "should we add X?" questions.
If a change conflicts with it, open an issue before writing code.

## Branches and releases

- `main` — **stable**. Receives merges from `beta` only when a version is
  promoted (version `X.Y.Z`), plus hotfixes. Each tag `vX.Y.Z` here is a
  **signed** release.
- `beta` — development. Pull requests target `beta`. Tags `vX.Y.Z-beta.N` are
  **unsigned** pre-releases.
- Hotfix: branch from `main`, open a PR to `main`, then merge `main` back into
  `beta`.
- After a stable `vX.Y.Z`, `beta` moves to the next minor (`X.(Y+1).0-beta.1`).

## Building and testing

```bash
./build.sh            # app only → build/bin/kai
./build.sh tests      # app + tests → build-dev/
ctest --test-dir build-dev --output-on-failure
```

The build must have no warnings (`-Wall -Wextra`). Run tests with Kai closed:
the CLI client test talks to a running instance.

## Rules

- C++20, Qt 6. Never block the GUI thread; `core/` and `engine/` don't depend
  on `ui/`.
- Write the test with the change. A bug fix needs a test that fails on the old
  code.
- Every user-facing string goes through `utils::tr("key")`, with the key in
  both `assets/i18n/en.json` and `pt.json` (`test_i18n_sync` checks it).
- Border radii use the design tokens, never hardcoded pixels.
- Short UI text. Explanations belong in `docs/`.
- More detail in [`AGENTS.md`](AGENTS.md).

## Authorship

Commits carry the real author only. Do not add `Co-Authored-By:` trailers or
"generated with" lines for AI tools; AI tools are not authors. By contributing
you agree your work is licensed under the [Apache License 2.0](LICENSE).

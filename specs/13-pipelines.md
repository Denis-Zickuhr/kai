# Spec 13: Pipelines (runbooks)

> **Status:** implemented for `1.0.0-beta.2` (core, runner, KIP children, editor, CLI `--from/--only`, Help, docs, sample).
> "Run from here" is a right-click on a step once the run ended; there is no separate Retry/Skip button (`on_fail: ask` covers it).

# Part A — Contract

## 1. Overview
A **pipeline** is a command (`type: pipeline`) whose body is an ordered list of
**steps**. Each step runs an *existing* command (shell, script, HTTP or KIP),
optionally guarded by a check, preceded by questions, retried, or followed by a
decision. The run is shown as one guided screen: a checklist with a live log per
step, prompts and confirmations in place, and **Retry / Skip / Run from here /
Stop** controls.

Because it is a command, a pipeline inherits everything commands have: tree
placement, filter, storage section, icon, description, buttons, parameters,
CLI (`kai <path>`), YAML export/import, folder actions, AI tools.

Motivating case: a long, guided routine that today is either a wall of shell or
one big KIP script — e.g. "ship to staging" (checks → tests → build → choose
target → confirm → deploy → wait for health → smoke test → rollback on
failure). As a pipeline it is a handful of small commands plus a YAML list.

### Naming
User-facing name: **Pipeline**. In code the type is `CommandType::Pipeline`,
but the engine classes are `Runbook`/`RunbookRunner`, because
`ExecutionPipeline` already means "main command + its hooks".

### Non-goals (firm — philosophy: not a CI/automation service)
- No parallel steps, matrices, artifacts, triggers or loops.
- Branching goes **forward only**. The only repetition is `retry`.
- A step cannot be a pipeline (no nesting in v1).
- Never self-acting: a pipeline starts only by an explicit run (click, CLI,
  panel). A pipeline with `ask`/`confirm` or a KIP step is interactive and
  therefore incompatible with `cron` and `autoRun` (validator + editor).

## 2. Data model

```yaml
- name: Ship to staging
  type: pipeline
  steps:
    - id: projects                 # unique in the pipeline; [a-z0-9_]+
      label: Projetos clonados     # default: the command's name
      run: Setup/maga-install      # command path (names, not ids)
      check: Setup/check-projects  # optional; exit 0 => already ok, skip run
      confirm: "Rodar maga local install?"
      ask: []                      # optional fields (KIP field syntax)
      params: { }                  # values for the step command's parameters
      when: []                     # ExecutionCondition rows
      when_combinator: and
      on_fail: abort               # abort | continue | ask | goto:<step id>
      retry: { times: 0, delay: 0, reask: false }
      wait_until: { check: Setup/api-online, timeout: 120, interval: 3 }
      capture:                     # variable name -> source
        TOKEN: { from: stdout, regex: "token=(\\S+)", secret: true }
        ID:    { from: json, path: "data.id" }
```

Field rules:
- `run` is required. `check`, `wait_until.check` and `run` are **command
  references** resolved by the path resolver used by the CLI
  (`cli-path-resolver`); relative paths resolve from the pipeline's folder.
  An unresolved reference makes *that step* fail with a clear message
  (fail-safe: never a crash, never a silent skip).
- `ask` uses the KIP field types (`text`, `secret`, `textarea`, `number`, `select`, `list`, `folderpick`,
  `filepick`, `flags`, …). Answers are exposed as `{{ask.<field>}}` to the
  rest of that step and later steps, and as `{{steps.<id>.ask.<field>}}`.
  `secret` answers are masked in logs/trace and never persisted
  (`remember: false` semantics).
- `confirm` shows a KIP confirm before the step runs; "No" skips the step
  (state `skipped`) — it is not a failure.
- `params` values accept `{{variables}}`, `{{ask.*}}`, `{{steps.*}}`.
- `capture.from`: `stdout` (last match of `regex`, or the last non-empty line),
  `exit`, `json` (dotted path over stdout), `response` (HTTP steps: body JSON
  path / header / status). Captured values become **run-scope variables**:
  `{{steps.<id>.<NAME>}}` and plain `{{NAME}}` for the following steps.
  `secret: true` masks them.
- Reserved per-step variables: `{{steps.<id>.exit}}`, `{{steps.<id>.ok}}`
  (`true`/`false`), `{{steps.<id>.state}}`.

## 3. Step execution order
For each step, in list order:

1. Evaluate `when`. False → `skipped` (quietly), next step.
2. `check` (if any). Exit 0 → `success` with detail "already ok", jump to 7
   (capture still runs against the check's output). An unresolved `check`
   reference is a failure, not "not done".
3. `confirm` (if any). No → `skipped`.
4. `ask` (if any). Cancel → the pipeline is cancelled (§6).
5. Run `run` with the step's resolved params. The command's **own** hooks,
   conditions, `ignoreExitCode`, working dir, terminal target and language
   apply exactly as when run alone — the rules live in the command.
6. `wait_until` (if any): poll its `check` every `interval` s until exit 0 or
   `timeout`. Timeout → failure.
7. `capture`, then `success`.

(`check` comes before `confirm`/`ask` on purpose: a step that is already done
must not bother the user with questions.)

**Verify:** if `check` exists and step 5 succeeded, `check` is run once more;
if it still fails, the step fails ("did not take effect"). `wait_until`
replaces this verification when present.

### Failure
A step fails when `run`/`wait_until` fails, a verify fails, or a reference is
unresolved. Then, in order:
1. `retry`: up to `times` more attempts, `delay` seconds apart. With
   `reask: true` the `ask` fields are shown again (prefilled, except secrets)
   before each attempt — this is the "wrong key, type another" loop.
2. `on_fail`:
   - `abort` (default): pipeline ends as failed; later steps stay `pending`.
   - `continue`: step stays `error`, pipeline goes on; final result is
     "completed with errors".
   - `ask`: a prompt offers **Retry / Skip / Abort**.
   - `goto:<id>`: jump to a **later** step (validator rejects backward or
     unknown targets).

## 4. KIP steps (required in v1)
A step whose `run` is a KIP command (`kip: true`) runs as a child
`KipSession`. While it is active:

- The pipeline view swaps its checklist for the **child's screen** (the same
  view component, bound to the child session) under a slim header
  "Step N/M — <label>" with the pipeline's Stop. When the child ends, the
  view returns to the checklist.
- The child's prompts/confirms/steps/tables behave exactly as in a standalone
  KIP run, including `remember`ed answers and chips.
- The child's `done` decides the step: `level: success` → `success`,
  `warning` → `success` with the text as detail, `error` → failure (§3
  Failure applies). Its `title/text` become the step detail; its
  `actions` are dropped (the pipeline's own `done` is the only final card).
- `set_env` is honored under the child command's `declaredEnvVars` allowlist,
  and the values are also visible to later steps as `{{NAME}}`.
- Cancelling inside the child cancels that step (failure path with
  `cancelled`); the user then gets the `on_fail` behaviour — `abort` ends the
  pipeline, `ask` offers Retry/Skip/Abort.
- `ask`, `confirm`, `check`, `capture.from: stdout` are allowed on KIP steps;
  `capture.from` for a KIP step reads the child's `done` text and `set_env`.

Not allowed as steps (validator error, same family as KIP's incompatibilities):
`interactiveTerminal`, `isBackground`, another `pipeline`. `formattedOutput`
and `compactOutput` are ignored inside a pipeline log.

## 5. Output of non-KIP steps
A plain command in a pipeline runs **panel-less**: it creates no Output tab;
its stdout/stderr stream (ANSI stripped, batched) into the step's log, which
opens automatically on error. HTTP steps log request line, status and timing;
the body goes to the log capped (the full response is available via capture).

## 6. Stop, cleanup, cancel
- **Stop** stops the running step's process group for real (philosophy §4),
  marks it `error (stopped)`, and ends the pipeline as *cancelled*.
- Each executed command's own `cleanup` hooks run as usual. The pipeline has
  no cleanup of its own in v1.
- Closing the view while running does not stop it (same as Output tabs);
  quitting Kai stops everything it started.
- Cancelling an `ask`/`confirm` prompt cancels the pipeline.

## 7. Controls and re-running
On the run screen, per step: **Retry** (error), **Skip** (error/pending),
**Run from here** (any step: restarts execution at that step, keeping earlier
captured variables and states), and a log toggle. On the pipeline: Stop,
Run again. CLI: `kai <path> --from <step id>` and `kai <path> --only <step id>`.
Captured/secret values from a previous run live only in memory for that
run view — never written to disk.

## 8. Result
Final card (KIP `done` style): success / completed with errors / failed at
step X / cancelled. The pipeline command has no actions of its own beyond the
standard result of a run; history records one run with per-step states.

## 9. Validation (kai-file-validator + editor)
- Unique step ids; `goto` forward and existing.
- References resolvable (warning at validation, error at run).
- No pipeline-in-pipeline; no forbidden step flags (§4).
- Incompatible with `interactiveTerminal`, `formattedOutput`, `compactOutput`,
  `openLastLink`, responders, `captureEnv`, `isBackground`; with `cron`/
  `autoRun` when interactive (§1).
- Unknown keys are ignored on read; a corrupted `steps` list degrades to
  zero steps with a log line (specs/07).

## 10. UI
- Command editor: type selector gains **Pipeline**; the Command tab is replaced
  by a **Steps** tab: read-only table, no header, step label only, pencil/trash
  per row (`makeRowActionsCell`), `+ Add` in the card header, drag to reorder,
  and `RowEditDialog` for the step (command picker, check picker, ask fields,
  params, when, on-fail, retry, wait, capture). Short labels; explanations
  live in `docs/pipelines.md` and Help.
- Helper commands: the editor offers "new step command" which creates a
  `hidden` command in a sibling folder; steps may also point to any visible
  command.
- Run view: reuses the KIP view (checklist + per-step log + prompts) fed by a
  `KipScreenState` driven by the runner; no new widget family except the
  step-controls row and the KIP-child stage header.
- All strings via `utils::tr` with keys in `en.json` and `pt.json`
  (`pipeline.*`); radii via tokens.

---

# Part B — Implementation plan

Order matters; each phase compiles, is tested and shippable.

### Phase 1 — Core
1. `core/models.h`: `CommandType::Pipeline`, `PipelineStep`, `PipelineCapture`,
   `Command::pipelineSteps`; JSON + YAML read/write (`project-format`), export/
   import by names; `kai-file-validator` rules (§9).
2. `engine/runbook-runner.{h,cpp}`: state machine over steps (§3), emitting
   into a `core::KipScreenState` (steps/step/prompt/confirm/done). No widgets.
   Runs step commands through `ExecutionPipeline` in panel-less mode, captures
   output, resolves references with `cli-path-resolver`.
3. Variable scope for the run (`ask`, `steps.*`, captures) layered into the
   existing `Global -> Folder -> Dynamic -> Parameters` hierarchy as a run
   scope; missing → empty string + log.
4. Tests: model round-trip, validator, runner with fake commands (check/skip,
   retry, on_fail variants, capture, forward goto, stop, unresolved ref).

### Phase 2 — Interaction
5. `ask`/`confirm`/`reask`, `when`, `on_fail: ask`, `wait_until`.
6. Run view wiring to the KIP view + step controls (Retry/Skip/Run from here).
7. CLI `--from/--only`; cron/autoRun guards.

### Phase 3 — KIP children (§4)
8. Runner hosts a child `KipSession`; view stage swap with header; `done`/
   `set_env` mapping; cancel path. Tests drive the child session without a
   process (`handleOutput`), as the KIP tests do.

### Phase 4 — Editor and docs
9. Steps tab, `RowEditDialog`, hidden-helper creation, type selector.
10. `docs/pipelines.md`, Help entry, creation-manifesto note for AI-authored
    pipelines, AI tool awareness (`run_command` refuses interactive pipelines
    like KIP; `run_in_app` opens them).
11. Add the "Ship to staging" scenario below to `sample/` (a demo project with
    fake scripts and a local HTTP stub, no real deploy) as the acceptance case.

### Acceptance scenario — "Ship to staging" (sample project)
Steps (each a small command in a hidden `steps/` folder, the deploy/health
parts faked by scripts so the sample runs anywhere):

1. `git-clean` — `check` only: fails if the working tree is dirty
   (`on_fail: ask` → Retry / Skip / Abort).
2. `tests` — runs the test script; `retry: {times: 1}`.
3. `build` — `check`: artifact already built for this commit → "already ok".
4. `choose-target` — **KIP step** (a select of target + a table of recent
   releases); `set_env` exposes `TARGET`.
5. `confirm-deploy` — `confirm` (danger) shown only `when` `TARGET == prod`.
6. `deploy` — captures `RELEASE_ID` from stdout; `secret` token comes from an
   `ask` field with `reask` (wrong token → asked again, up to 3 times).
7. `health` — `wait_until` an HTTP command (the stub) returns 200,
   `timeout: 60`; on timeout `on_fail: goto:rollback`.
8. `smoke` — HTTP step with `capture` (`json: data.version`); asserts it equals
   the deployed version, else `on_fail: goto:rollback`.
9. `announce` — `continue` on failure; skipped when the pipeline went to
   rollback.
10. `rollback` — reached only via `goto`; skipped (`when`) on the happy path.

Expected behaviour:
- Happy path: every step goes green; steps 1 and 3 show "already ok" on a
  re-run with nothing changed.
- Dirty tree: step 1 offers Retry/Skip/Abort; after cleaning, Retry continues.
- Wrong token three times: step 6 fails, pipeline aborts, token never appears
  in logs or the protocol trace.
- Stub down at step 7: timeout jumps to `rollback`; the final card says
  "failed at health — rolled back".
- Fix the cause and use **Run from here** on `deploy`: earlier steps are not
  redone and `TARGET` is still set.
- Stop during `deploy` kills its process group; the command's `cleanup` runs.

### Docs to update in the same change
`docs/philosophy.md` (item 10 chained flows → done via pipelines; add the
non-goals above), `docs/roadmap-ideas.md` (A3 → pipelines; freeze note),
`docs/kip.md` (KIP as a pipeline step), `docs/commands.md`.

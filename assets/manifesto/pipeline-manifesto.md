# The pipeline manifesto — writing a Kai pipeline

You are writing a **pipeline** for Kai: a `type: pipeline` command in a `kai.yml` whose body is a list of `steps`.
Each step runs ANOTHER command of the same file. Kai shows the pipeline as a checklist with a live log per step.
Use it for long, guided routines: setup, deploy, release, rollback. This guide covers only pipelines; for the rest of
the file (variables, parameters, HTTP commands, icons) follow the Kai manifesto, which goes with it.

Answer with ONE complete `kai.yml` (YAML only, no JSON). Do not invent keys: every key below is the whole model.

## 1. The shape

1. The pipeline itself is a command with `type: pipeline` and `steps`. It has NO `command` of its own.
2. Every step references another command **by name** (`run: Build`) or by `Folder/Name` (`run: steps/Build`).
   Never by id. Names are case-sensitive and must exist in the file.
3. The commands a pipeline calls are ordinary commands. Put them in a subfolder (`folder: steps`) and mark them
   `hidden: true` so the sidebar shows only the pipeline.
4. A step command cannot be a pipeline, a background command or an `interactive_terminal` one. A KIP command is allowed.

## 2. A step

Only `id` and `run` are required.

```yaml
steps:
  - id: deploy            # unique in the pipeline: a-z, 0-9, _
    label: Deploy         # shown in the checklist (default: the command name)
    run: steps/Deploy     # the command to run
    check: steps/Is deployed   # exit 0 = already done, `run` is skipped; also run again after `run` to verify
    confirm: "Deploy to production?"   # asks first; answering No skips the step
    ask:                  # questions shown before `run`; answers are {{ask.<name>}}
      - name: token
        label: Deploy token
        type: secret      # masked in logs, never saved
    params:               # values for the step command's parameters; {{variables}} accepted
      TOKEN: "{{ask.token}}"
    when:                 # run only if every condition passes (or any, with when_combinator: or)
      - left: "{{TARGET}}"
        op: eq
        right: prod
    when_combinator: and
    on_fail: abort        # abort (default) | continue | ask | goto:<later step id>
    retry:
      times: 2            # extra attempts before on_fail applies
      delay: 1            # seconds between attempts
      reask: true         # show `ask` again each attempt
    wait_until:           # after `run`, poll this command until it exits 0
      check: steps/Health
      timeout: 60
      interval: 3
    capture:              # variables read from the step output
      RELEASE_ID:
        from: json        # stdout | json | exit | status
        path: data.id     # json only
```

- `ask` fields: `name` (required, letters, digits, `_`), `label`, `type` (`text` default, `secret`, `textarea`,
  `number`, `select` with `options: [a, b]`, `filepick`, `folderpick`), `default`, `required`.
- `capture` with `from: stdout` takes the last line, or group 1 of the last `regex` match. Captures are available later
  as `{{NAME}}` and `{{steps.<id>.NAME}}`. Add `secret: true` to mask one.
- Every step leaves `{{steps.<id>.ok}}` (`true`/`false`) and `{{steps.<id>.exit}}`.

## 3. Patterns

- **Idempotent step**: give it a `check` so a re-run skips what is already done.
- **Safe deploy**: `confirm` on the risky step, `retry` for flaky ones, `wait_until` for a health check.
- **Rollback**: `on_fail: goto:rollback` on the risky step, and `when: {{steps.<id>.ok}} eq false` on the rollback
  step so it only runs after a failure. The rollback step is the LAST one.
- **Secrets**: ask them (`type: secret`) and pass through `params`; never write them in the file.
- **Tolerated failure**: `on_fail: continue` for optional steps (cache warm-up, notifications).

## 4. Pitfalls — Kai rejects or ignores these

- A step without `id` or `run`; an `id` with capitals, spaces or `-`; two steps with the same `id`.
- `goto` to an unknown step or to an EARLIER one: it only moves forward. A pipeline never loops except via `retry`.
- `run` naming a command that does not exist, or one that is itself a pipeline.
- A pipeline without `steps`, or with a `command:` instead of them.
- An `ask` field without `name`.
- A pipeline that asks questions cannot be scheduled (`cron_expression`) or `auto_run`.
- Numbers and booleans in values are text for Kai: quote anything that must stay a string (`"1"`).

## 5. Checklist

- [ ] Each step command exists, is not a pipeline/background/interactive, and the pipeline references it by name.
- [ ] Step ids are unique `a-z0-9_`; every `goto` points to a later step.
- [ ] Risky steps have `confirm`, a `check` or a rollback; secrets come from `ask` with `type: secret`.
- [ ] Helper commands are `hidden: true` in a subfolder.
- [ ] The output is a single valid `kai.yml`.

## 6. Complete example

```yaml file
commands:
  - name: Ship
    type: pipeline
    steps:
      - id: build
        run: steps/Build
        check: steps/Is built
      - id: deploy
        run: steps/Deploy
        confirm: "Deploy to production?"
        ask:
          - name: token
            label: Deploy token
            type: secret
        params:
          TOKEN: "{{ask.token}}"
        retry:
          times: 2
          reask: true
        wait_until:
          check: steps/Health
          timeout: 60
        on_fail: goto:rollback
      - id: rollback
        run: steps/Rollback
        when:
          - left: "{{steps.deploy.ok}}"
            op: eq
            right: "false"
  - name: Build
    folder: steps
    type: command
    command: make build
    hidden: true
  - name: Is built
    folder: steps
    type: command
    command: test -f build/app
    hidden: true
  - name: Deploy
    folder: steps
    type: command
    command: ./deploy.sh
    hidden: true
  - name: Health
    folder: steps
    type: command
    command: curl -fsS http://localhost:8080/health
    hidden: true
  - name: Rollback
    folder: steps
    type: command
    command: ./rollback.sh
    hidden: true
```

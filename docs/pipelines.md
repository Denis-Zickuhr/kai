# Pipelines

A **pipeline** is a command whose body is a list of **steps**. Each step runs *another* command of yours (shell,
Python/Node/PHP, HTTP or KIP), with optional checks, questions, retries and decisions. Kai shows the run as one guided
screen: a checklist with a live log per step, and prompts in place.

Use it for long, attended routines — a setup, a deploy, a release. It is not a CI system: steps run one at a time, on this
machine, while you watch, and nothing starts by itself (a pipeline never runs from a schedule or auto-run).

In the editor pick **Pipeline** as the type and add steps in the **Steps** tab. The same thing in `kai.yml` (see the
[creation manifesto](../assets/manifesto/kai-manifesto.md) §9.5 and `sample/pipeline`):

```yaml
- name: Ship to staging
  type: pipeline
  steps:
    - id: build
      run: steps/build            # a command, by name or Folder/Name — never an id
      check: steps/is-built       # exit 0 = already done: "Already ok", run is skipped
    - id: deploy
      run: steps/deploy
      ask:                        # KIP fields; answers are {{ask.token}}
        - { name: token, type: secret, label: Token }
      params: { TOKEN: "{{ask.token}}" }
      retry: { times: 2, reask: true }
      capture:
        RELEASE_ID: { from: stdout, regex: "release=(\\w+)" }
    - id: health
      run: steps/noop
      wait_until: { check: steps/health, timeout: 60, interval: 3 }
      on_fail: goto:rollback
    - id: rollback
      run: steps/rollback
      when:
        - { left: "{{steps.health.ok}}", op: eq, right: "false" }
```

## What a step does

In this order:

1. **when** — conditions (same format as [execution conditions](hooks.md)); false = skipped.
2. **check** — exit 0 means it is already done: the step is green ("Already ok") and **run** is skipped.
3. **confirm** — asks first; *No* skips the step.
4. **ask** — shows the fields; the answers become `{{ask.<name>}}`. A `secret` answer is masked in logs and never saved.
5. **run** — the command. Its own hooks, conditions, terminal profile and language apply as if you ran it alone.
6. **wait_until** — polls a command until it exits 0, or fails after `timeout`.
7. **verify** — with a `check`, it runs once more; if it still fails the step failed ("Did not take effect").
8. **capture** — reads values from the output into variables.

A step runs a *command*; it can be a KIP command too — its screen takes over the pipeline view while it runs, and its
`done` result becomes the step's result. A step cannot be another pipeline, a background command or an interactive
terminal.

## When a step fails

`retry` runs it again (`times`, `delay` seconds, and `reask` to show **ask** again — handy for a wrong key). Then
`on_fail` decides:

| `on_fail` | Effect |
|---|---|
| `abort` (default) | The pipeline stops: "Failed at <step>". |
| `continue` | The step stays red, the pipeline goes on: "Finished with errors". |
| `ask` | Choose **Try again / Skip / Abort**. |
| `goto:<id>` | Jump to a *later* step (a rollback). Forward only. |

## Variables

- `{{ask.<name>}}` — answers; `{{NAME}}` / `{{steps.<id>.NAME}}` — captures (`stdout` with a `regex`, `json` with a dotted
  `path`, `exit`, `status`; add `secret: true` to mask).
- `{{steps.<id>.ok}}` (`true`/`false`) and `{{steps.<id>.exit}}` exist for every step that ran — guard a rollback with them.
- The run's variables live only for that run; they never leak into your project's dynamic variables (a KIP step's
  `set_env` still exports as usual).

## Running

- **Stop** ends the current step for real (its process group) and the pipeline is *cancelled*; each command's own
  `cleanup` hooks still run.
- Re-running starts over. When a run has ended, right-click a step and choose **Run from here** to resume, keeping the
  captured variables.
- From the terminal, `kai -g <path>` runs a pipeline through the open app (like a KIP command); `--from <step>` starts at
  a step and `--only <step>` runs just that one.

Put the step commands in a subfolder and mark them **Hidden** so they stay out of the tree.

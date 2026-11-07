# Pipeline + KIP sample

Three pipelines that mix plain steps with **KIP steps** (wizards, confirmations, checklists). Each KIP step takes over
the run screen while it runs; its `set_env` values reach the later steps as `{{NAME}}`. Everything is fake: scripts only
touch `.state/` in this folder. KIP steps use the injected `kip` function, so they need a POSIX shell (Linux, macOS, WSL).

## Release
Wizard (KIP) → Package → Approval (KIP, **stable only**) → Publish (KIP) → Announce.
- Pick `beta`: the Approval step is skipped. Pick `stable`: a danger confirm appears; **Not yet** fails the step.
- Untick "Announce when done": the Announce step is skipped. Set `FAIL_ANNOUNCE=1` in the project variables: it fails
  but the run still finishes (`on_fail: continue`).

## Restore database
Pick a backup (KIP) → Verify → Snapshot → Restore (KIP, danger confirm) → Smoke test.
- The picker's choice reaches **Verify** and **Restore** through `{{BACKUP_FILE}}`.
- Pick **Corrupt** to stop at Verify. Run it twice: the Snapshot step is skipped the second time (`check`).

## Health sweep
Disk, Network and Toolchain never abort; the final KIP **Report** is built from `{{steps.<id>.ok}}`.
- `FAIL_NET=1` in the project variables, or `touch .state/missing-tool`, turns a check red and the report says so.

Use **Reset sample state** between runs.

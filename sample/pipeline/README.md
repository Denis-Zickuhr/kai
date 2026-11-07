# Pipeline sample

`Ship to staging` is a pipeline: a list of steps that run the hidden commands in `steps/`. All scripts are fake and only
touch `.state/` in this folder.

Things to try (open the project in Kai, run **Ship to staging**):

- **Happy path**: pick `staging`, enter the token `letmein`. Every step goes green; run it again and **Build** says
  "Already ok".
- **Wrong token**: the Deploy step asks again (the token is never pre-filled), up to 3 times.
- **Production gate**: pick `prod` — the confirm step appears; answer No and it is skipped.
- **Retry / Skip / Abort**: `touch .state/dirty` before running; the first step offers the choice.
- **Rollback**: run with `FAIL_HEALTH=1` in the project's environment variables — the health check times out, jumps to
  **Rollback** and the pipeline ends "Failed at Health check".
- **Stop** during Deploy kills its processes.

Use **Reset sample state** between runs.

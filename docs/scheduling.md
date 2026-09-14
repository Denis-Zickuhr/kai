# Schedules & auto-run

A command can start by itself — on a schedule, or when Kai starts. Both options are in the command editor's **Configuration** tab and go through the same pipeline as a click: hooks, conditions, notifications and run history all apply.

## Schedule (CRON)

Type a standard 5-field cron expression: `minute hour day-of-month month day-of-week`.

```
0 9 * * 1-5      # 9:00, Monday to Friday
*/15 * * * *     # every 15 minutes
30 2 1 * *       # 2:30 on the 1st of each month
0 8 * * mon,fri  # 8:00 on Mondays and Fridays
```

Supported: `*`, lists (`1,2,3`), ranges (`1-5`), steps (`*/15`) and day/month names (`mon`, `jan`). The editor shows a plain-language reading under the field (*Weekdays at 09:00*). As in classic cron, when both day-of-month and day-of-week are set, either one matching fires it.

> **Tip:** **Times are evaluated in UTC, not in your local time zone.** If you are at UTC−3, `0 9 * * *` fires at 06:00 local time.

- **Notify when executed** sends a [notification](notifications.md) with the result at every run; off by default so a frequent schedule doesn't spam you.
- Kai has to be running (it lives in the tray) for a schedule to fire.

## Run when Kai starts

**Run when Kai starts** fires the command automatically after the **Delay** you set (seconds). It uses the **last values** of the parameters; if a required parameter has no history, the command is skipped and the reason is logged (the boot never stops to ask a question).

```
{ "name": "Sync", "type": "command", "command": "./sync.sh", "is_background": true,
  "auto_run": true, "auto_run_delay_sec": 5 }
```

## Not combinable with

[KIP](kip.md) commands can't be scheduled or auto-run (they need a screen to talk to).

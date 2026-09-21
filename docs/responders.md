# Auto-replies

An **auto-reply** (auto-responder) answers a prompt in a command's output **for you**: when the output matches a pattern, Kai types the response into the command's input. Use it for tools that ask *Continue? [y/N]* and have no flag to skip it. Add them in the command editor's **Auto-replies** tab.

## Fields

|   |   |
|---|---|
| **Name** | required — a responder without a name is neither saved nor activated |
| **Enabled** | switch one off without deleting it |
| **Condition (regex)** | a regular expression tested against the output; captured groups are available as `\1`, `\2`… |
| **Response** | the text sent, as if you had typed it. Use `\1` to reuse a captured group and `{{VAR}}` to interpolate variables |
| **Limit triggers** / **Max triggers** | fire at most N times per run, so a repeated prompt can't loop forever |

## Example

```
Condition: Type the name of the database \((\w+)\) to confirm:
Response:  \1
```

Every automatic answer is echoed in the output so you can see who typed it.

## Good to know

- It reads the live output, so it works with background commands too.
- Not available on [KIP](kip.md) commands (the program already talks to Kai through the protocol).
- In [kai.json](kai-json.md) they are the `responders` list.

# Dynamic / faker variables

Tokens with a `$` prefix are resolved **at execution time**.

| Token | Result |
|-------|-----------|
| `{{$uuid}}` | UUID v4 |
| `{{$timestamp}}` | epoch in seconds |
| `{{$timestampMs}}` | epoch in milliseconds |
| `{{$isoTimestamp}}` | ISO-8601 date/time (UTC) |
| `{{$randomInt}}` | integer in `[0, 100000)` |
| `{{$randomInt.N}}` | integer in `[0, N)` |
| `{{$randomUuidHex}}` | 32 hex chars, no separators |

## Example

```json
{
  "id": "{{$uuid}}",
  "createdAt": {{$timestamp}},
  "nonce": {{$randomInt.1000}}
}
```

Unknown tokens fall back safely (empty string + a log warning).

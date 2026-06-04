# Examples

| Example | Description |
| --- | --- |
| `Basic` | Minimal init, realtime log printing, and manual flush. |
| `JsonPayloads` | Compact and pretty ArduinoJson payload logging. |
| `PrintfFormatting` | `printf`-style logging helpers. |
| `CallbacksAndFlush` | Realtime observation and persistence callback behavior. |
| `Diagnostics` | Runtime counters and query helpers. |
| `TempoTimestamps` | Full, minimal, and custom Tempo timestamp formatting. |
| `OverflowPolicies` | Pending queue limits and overflow policy configuration. |

Start with `examples/Basic` for the shortest complete flow.

Use `examples/CallbacksAndFlush` when wiring Trace into storage. The flush callback receives a `TraceLogBatch` and should return `TraceFlushResult::Ok` only after the batch has been accepted by the storage layer.

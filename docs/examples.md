# Examples

| Example | Description |
| --- | --- |
| `Basic` | Minimal init, realtime log printing, and manual flush. |
| `JsonPayloads` | Compact and pretty ArduinoJson payload logging. |
| `PrintfFormatting` | `printf`-style logging helpers. |
| `CallbacksAndFlush` | Realtime observation and persistence callback behavior. |
| `Diagnostics` | Runtime counters, Strata placement requests, and observed memory regions. |
| `MemoryPolicy` | PSRAM-preferred general storage and task stack with an internal realtime override. |
| `TempoTimestamps` | Full, minimal, and custom Tempo timestamp formatting. |
| `OverflowPolicies` | Pending queue limits and overflow policy configuration. |

Start with `examples/Basic` for the shortest complete flow.

Use `examples/MemoryPolicy` when deciding how Trace should consume internal RAM versus PSRAM. The library defaults already prefer external memory for all movable owned allocations; the example demonstrates the optional realtime override.

Use `examples/CallbacksAndFlush` when wiring Trace into storage. The flush callback receives a `TraceLogBatch` and should return `TraceFlushResult::Ok` only after the batch has been accepted by the storage layer.

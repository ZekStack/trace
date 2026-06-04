# Troubleshooting

## Logging returns `NotInitialized`

Call `trace.init()` before logging. If `init()` fails, inspect `TraceResult::message`.

## `flushAndWait()` times out

The internal task may be blocked in `onLog()` or `onFlush()`. Keep callbacks short and avoid calling Trace logging methods recursively from callbacks.

## Pending logs keep growing

Register an `onFlush()` callback and return `TraceFlushResult::Ok` when persistence succeeds. A failed or retry result leaves pending logs queued.

## Logs are missing from persistence

Check `TraceDiag::droppedLogCount`. Pending logs are bounded by `maxPendingLogs`, and overflow behavior depends on `TraceConfig::overflowPolicy`.

## Tempo timestamps are missing

Call `attachTempo()` after Tempo is initialized. For `TraceTimeFormat::Custom`, provide a non-null formatter that writes a non-empty string and returns `true`.

## PSRAM task stack init fails

`TraceStackType::Psram` requires ESP-IDF task-stack allocation support and available PSRAM. Use `TraceStackType::Auto` to prefer PSRAM but fall back to internal RAM.

# Troubleshooting

## Logging returns `NotInitialized`

Call `trace.init()` before logging. If `init()` fails, inspect `TraceResult::message`.

## `flushAndWait()` times out

The internal task may be blocked in `onLog()` or `onFlush()`. Keep callbacks short and avoid calling Trace logging methods recursively from callbacks.

## Pending logs keep growing

Register an `onFlush()` callback and return `TraceFlushResult::Ok` when persistence succeeds. `TraceFlushResult::Failed` leaves pending logs queued for a future flush attempt. `TraceFlushResult::Retry` leaves pending logs queued and schedules another attempt after `retryIntervalMs`.

## Logs are missing from persistence

Check `TraceDiag::droppedLogCount`. Pending logs are bounded by `maxPendingLogs`, and overflow behavior depends on `TraceConfig::overflowPolicy`.

## Realtime logs are missing

Check `TraceDiag::droppedRealtimeLogCount`. Realtime delivery is bounded by `maxRealtimeLogs` and is independent from `maxRecentLogs`.

`maxRealtimeLogs = 0` disables realtime buffering and delivery. If no stream and no `onLog()` callback are configured, Trace skips realtime queueing.

## Tempo timestamps are missing

Call `attachTempo()` after Tempo is initialized. For `TraceTimeFormat::Custom`, provide a non-null formatter that writes a non-empty string and returns `true`.

Keep the attached Tempo alive until `Trace::end()` completes. Detaching while Trace is active does not synchronize already snapshotted worker use.

## PSRAM task stack init fails

`TraceStackType::Psram` requires ESP-IDF task-stack allocation support and available PSRAM. Use `TraceStackType::Auto` to prefer PSRAM but fall back to internal RAM.

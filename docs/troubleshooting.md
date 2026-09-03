# Troubleshooting

## Logging returns `NotInitialized`

Call `trace.init()` before logging. If `init()` fails, inspect `TraceResult::status` and `TraceResult::message`.

```cpp
TraceResult result = trace.init();
if (!result) {
	Serial.println(result.message);
}
```

## `init()` returns `OutOfMemory`

Check the configured Strata placement.

`Strata::Placement::RequireExternal` never falls back. If an enabled Trace-owned allocation requires external memory and PSRAM is unavailable or exhausted, initialization fails.

For normal product defaults, use `PreferExternal`:

```cpp
config.memory.allocation = Strata::Placement::PreferExternal;
```

This prefers PSRAM and falls back to internal memory.

If only realtime storage requires external memory, remember that a disabled realtime queue (`maxRealtimeLogs = 0`) allocates no ring storage and therefore does not fail merely because its placement is `RequireExternal`.

## Task creation fails

`TraceStatus::TaskCreateFailed` can occur when the requested stack cannot be created.

The default is:

```cpp
config.memory.taskStack = Strata::Placement::PreferExternal;
```

Use `Internal` if the task stack must stay in internal RAM. Use `RequireExternal` only when failing without a PSRAM stack is intentional.

Stack sizes are FreeRTOS byte sizes and must be at least 1024 bytes and properly aligned.

## How do I know where memory actually landed?

Use `TraceDiag` regions rather than inferring from the requested placement:

```cpp
TraceDiag diag = trace.getDiagnostics();
Serial.println(Strata::toString(diag.requestedAllocationPlacement));
Serial.println(Strata::toString(diag.recentStorageRegion));
Serial.println(Strata::toString(diag.taskStackRegion));
```

`PreferExternal` is a request with fallback, so the observed region may be `Internal`.

## I want realtime work internal but history in PSRAM

Use the realtime override:

```cpp
TraceConfig config;
config.memory.allocation = Strata::Placement::PreferExternal;
config.memory.taskStack = Strata::Placement::PreferExternal;
config.realtimeAllocation = Strata::Placement::Internal;
```

`realtimeAllocation = std::nullopt` means inherit the general allocation placement.

## `flushAndWait()` times out

The internal task may be blocked in `onLog()` or `onFlush()`. Keep callbacks short and avoid recursively calling Trace logging methods from callbacks.

A retrying flush remains pending until it succeeds, fails, or the caller's timeout expires.

## `end()` times out

A timed `end()` does not abandon Strata-owned task storage. Trace retains task and buffer ownership so cleanup can be attempted again.

Call `end()` again after the blocking callback/flush condition is resolved. The Trace destructor performs an unbounded cleanup so the worker cannot outlive its implementation state.

## Pending logs keep growing

Register an `onFlush()` callback and return `TraceFlushResult::Ok` when persistence succeeds. `Failed` and `Retry` both leave pending records queued.

Check `TraceDiag::pendingLogCount`, `droppedLogCount`, and the flush counters.

## Logs are missing from persistence

Check `TraceDiag::droppedLogCount`. Pending storage is bounded by `maxPendingLogs`, and overflow behavior is controlled by `TraceConfig::overflowPolicy`.

## Realtime logs are missing

Check `TraceDiag::droppedRealtimeLogCount`.

Realtime delivery is bounded by `maxRealtimeLogs` and independent from recent history. `maxRealtimeLogs = 0` disables realtime buffering. If no stream and no `onLog()` callback are configured, Trace skips realtime queueing.

## Tempo timestamps are missing

Call `attachTempo()` after Tempo is initialized. For `TraceTimeFormat::Custom`, provide a non-null formatter that writes a non-empty string and returns `true`.

Keep the attached Tempo alive until `Trace::end()` completes.

## Code using v0.2.x memory enums no longer compiles

Trace v0.3.0 intentionally removed `TraceStackType` and `TraceStorageMemory` in favor of the shared Strata policy.

Typical migration:

```cpp
// v0.2.x
// config.stackType = TraceStackType::Auto;
// config.storageMemory = TraceStorageMemory::PreferPsram;
// config.realtimeStorageMemory = TraceStorageMemory::Internal;

// v0.3.0
config.memory.taskStack = Strata::Placement::PreferExternal;
config.memory.allocation = Strata::Placement::PreferExternal;
config.realtimeAllocation = Strata::Placement::Internal;
```

## Query code using `std::vector<TraceLog>` no longer compiles

Use the public alias:

```cpp
TraceLogList logs = trace.getLogs();
```

`TraceLogList` is `Strata::Vector<TraceLog>` so Trace-owned query storage follows the configured allocation placement.

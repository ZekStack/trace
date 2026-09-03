# Configuration

`TraceConfig` controls Trace-owned memory placement, the internal task, bounded queues, flush timing, overflow behavior, JSON formatting, level filtering, and stream colors.

## Defaults

```cpp
TraceConfig config;

config.memory = {
	.allocation = Strata::Placement::PreferExternal,
	.taskStack = Strata::Placement::PreferExternal,
};
config.realtimeAllocation = std::nullopt;

config.stackSize = 4096;
config.priority = 1;
config.coreId = tskNO_AFFINITY;
config.maxRecentLogs = 100;
config.maxRealtimeLogs = 100;
config.maxPendingLogs = 50;
config.maxFlushBatchLogs = 0;
config.flushEveryLogs = 20;
config.flushIntervalMs = 30000;
config.retryIntervalMs = 1000;
config.flushOnError = true;
config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
config.jsonFormat = TraceJsonFormat::Compact;
config.minLevel = TraceLevel::Debug;
config.enableColors = true;
config.blockCallerTimeoutMs = 1000;
config.maxTagLength = 32;
config.maxMessageLength = 256;
config.maxFormattedLength = 384;
```

## Strata memory policy

Trace v0.3.0 uses the same memory vocabulary as other Strata-integrated ZekStack libraries.

`memory.allocation` controls movable general Trace-owned allocations:

* recent log ring storage;
* pending log ring storage;
* query result vectors and `TraceLog` strings;
* flush batch vectors and strings;
* general callback holders;
* other movable Trace-owned allocations.

`memory.taskStack` controls the Trace worker task stack.

`realtimeAllocation` is an optional override for realtime-owned allocations:

* realtime ring storage;
* realtime callback holder storage;
* realtime `TraceLog` conversion and formatting.

When `realtimeAllocation` is `std::nullopt`, it inherits `memory.allocation`.

The defaults are intentionally PSRAM-first:

```cpp
config.memory.allocation = Strata::Placement::PreferExternal;
config.memory.taskStack = Strata::Placement::PreferExternal;
config.realtimeAllocation = std::nullopt;
```

`PreferExternal` uses external memory when available and falls back to internal memory. It is suitable as the default for products that may run on ESP32 variants without PSRAM.

`Internal` forces the allocation into internal-capable memory.

`RequireExternal` refuses to fall back. `init()` fails if an enabled required allocation cannot be satisfied externally.

Example: keep realtime work internal but place large history/persistence storage and the task stack in PSRAM where possible:

```cpp
TraceConfig config;
config.memory.allocation = Strata::Placement::PreferExternal;
config.memory.taskStack = Strata::Placement::PreferExternal;
config.realtimeAllocation = Strata::Placement::Internal;
```

RTOS control structures that Strata requires internally remain internal regardless of Trace's movable-memory policy.

## Requested placement vs observed region

A placement is a request. A region is what Strata observed after allocation.

```cpp
TraceDiag diag = trace.getDiagnostics();

Strata::Placement requested = diag.requestedAllocationPlacement;
Strata::Region actual = diag.recentStorageRegion;
```

Use `Strata::toString()` when presenting these diagnostics.

Available Trace diagnostics include:

* `requestedAllocationPlacement`;
* `requestedRealtimeAllocationPlacement`;
* `requestedTaskStackPlacement`;
* `taskStackRegion`;
* `recentStorageRegion`;
* `realtimeStorageRegion`;
* `pendingStorageRegion`;
* allocated byte counts for each ring buffer.

Disabled queues allocate no ring storage and report `Strata::Region::Unknown`.

## Buffers

`maxRecentLogs` controls queryable in-RAM history. `0` disables recent history.

`maxRealtimeLogs` controls realtime delivery used by `onLog()` and stream output. `0` disables realtime buffering.

`maxPendingLogs` controls unsaved logs waiting for `onFlush()`. `0` disables persistence buffering; accepted logs are still available to enabled recent/realtime paths and count as dropped for persistence.

Each enabled queue allocates its fixed-capacity `TraceRecord` backing once during `init()`.

Approximate fixed queue storage is:

```txt
sizeof(TraceRecord) * (maxRecentLogs + maxRealtimeLogs + maxPendingLogs)
```

Runtime payload caps do not shrink `TraceRecord`; compile-time caps do.

## Allocation behavior

The direct C-string enqueue path remains deliberately allocation-free after successful `init()` when target queues have capacity and no output-boundary conversion is triggered.

Public/output-boundary values are Strata-backed in v0.3.0:

* `TraceLog::tag`, `message`, `formatted`, and `timeText` are `Strata::String`;
* query methods return `TraceLogList`, an alias of `Strata::Vector<TraceLog>`;
* `TraceLogBatch::logs` is a `TraceLogList`.

This means Trace-owned query, flush, and realtime conversion allocations follow the configured Strata policy instead of the platform default C++ allocator.

Caller-owned values still follow the caller's allocator. For example, allocations made while constructing a lambda capture or a caller-owned `std::string` are outside Trace's ownership boundary.

## Payload limits

`maxTagLength` applies to tags.

`maxMessageLength` applies to direct message logging APIs.

`maxFormattedLength` applies to `printf`-style and JSON-formatted input before it becomes a log message.

Runtime payload limits are bounded by:

```cpp
TRACE_RECORD_MAX_TAG_LENGTH
TRACE_RECORD_MAX_MESSAGE_LENGTH
TRACE_FORMATTED_BUFFER_LENGTH
TRACE_TIME_TEXT_BUFFER_LENGTH
```

A runtime limit of `0` uses the compiled maximum. Truncated records set `TraceLog::truncated` and increment `TraceDiag::truncatedLogCount` once per record.

## Flush triggers

Trace flushes pending logs when:

* `flush()` or `flushAndWait()` requests it;
* `pendingLogCount >= flushEveryLogs`;
* `flushIntervalMs` elapses with pending logs;
* `flushOnError` is enabled and an `Error` or `Fatal` log is queued.

`retryIntervalMs` controls the delay after `TraceFlushResult::Retry`.

`maxFlushBatchLogs` controls how many public logs are converted for each callback invocation. `0` means uncapped. A capped flush cycle may invoke `onFlush()` multiple times, oldest to newest.

`flushAndWait()` waits until all pending logs that existed when the call started have been successfully flushed. Newer logs may remain for a later cycle.

## Flush results

`TraceFlushResult::Ok` removes the successful batch.

`TraceFlushResult::Failed` retains pending logs, increments failure diagnostics, and makes the waiting flush fail.

`TraceFlushResult::Retry` retains pending logs, increments retry diagnostics, and schedules the next attempt after `retryIntervalMs`.

## Overflow policies

`DropOldestPending` removes the oldest pending log and queues the new record.

`DropNewest` keeps the recent/realtime record but refuses the new pending record.

`BlockCaller` requests a flush and waits up to `blockCallerTimeoutMs` for pending space.

`FlushImmediately` requests an immediate flush and waits for pending capacity up to `blockCallerTimeoutMs`.

## Task ownership and shutdown

The worker task is owned by `Strata::FreeRTOS::Task`. Strata uses static task ownership so the task cannot free its own caller-owned stack/control storage.

When Trace stops, the worker publishes completion and suspends. `Trace::end()` then deletes/resets the task externally and only afterwards releases queue storage. A timed-out `end()` leaves ownership intact so a later `end()` or the destructor can finish cleanup safely.

The destructor performs an unbounded cleanup to prevent the worker task from outliving Trace-owned state.

Stack sizes are ESP32 FreeRTOS byte sizes and must be at least 1024 bytes.

## Stream colors

`enableColors` controls ANSI color codes for `setStream()` output and defaults to `true`.

Callbacks, flush batches, and query helpers keep `TraceLog::formatted` as plain text.

## v0.2.x migration

| v0.2.x | v0.3.0 |
| --- | --- |
| `TraceStackType::Auto` | `Strata::Placement::PreferExternal` |
| `TraceStackType::Internal` | `Strata::Placement::Internal` |
| `TraceStackType::Psram` | `Strata::Placement::RequireExternal` |
| `TraceStorageMemory::Internal` | `Strata::Placement::Internal` |
| `TraceStorageMemory::PreferPsram` | `Strata::Placement::PreferExternal` |
| `TraceStorageMemory::RequirePsram` | `Strata::Placement::RequireExternal` |
| `stackType` | `memory.taskStack` |
| `storageMemory` | `memory.allocation` |
| `realtimeStorageMemory` | `realtimeAllocation` |

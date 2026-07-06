# Configuration

`TraceConfig` controls the internal task, log storage memory, log limits, flush timing, overflow behavior, JSON formatting, level filtering, and stream colors.

```cpp
TraceConfig config;
config.stackSize = 4096;
config.priority = 1;
config.coreId = tskNO_AFFINITY;
config.stackType = TraceStackType::Auto;
config.storageMemory = TraceStorageMemory::Internal;
config.maxRecentLogs = 100;
config.maxRealtimeLogs = 100;
config.maxPendingLogs = 50;
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

## Buffers

`maxRecentLogs` controls queryable in-RAM history only. Recent logs stay available until overwritten by newer logs. `maxRecentLogs = 0` disables query history.

`maxRealtimeLogs` controls the delivery queue used by `onLog()` and stream output. It is independent from recent history. `maxRealtimeLogs = 0` disables realtime buffering and delivery.

`maxPendingLogs` controls unsaved logs waiting for `onFlush()`. Pending logs are cleared only after the flush callback returns `TraceFlushResult::Ok`.

`maxPendingLogs = 0` disables persistence buffering. Logs are still accepted for recent history and realtime delivery, but they are counted as dropped for persistence.

Queue-count `0` means disabled. Payload-cap `0` means use the compiled maximum for that payload type.

## Storage memory

Trace stores recent, realtime, and pending logs in fixed-capacity ring buffers. Each enabled queue is allocated once during `init()`.

After `Trace::init()`, accepted direct C-string log calls do not allocate on the internal enqueue path when all target queues have capacity and no output-boundary conversion is triggered.

This guarantee is intentionally narrow. Query APIs allocate `std::vector<TraceLog>`, flush batch conversion and `onLog()` conversion can allocate because public `TraceLog` contains `std::string`, stream implementations may allocate internally, and user code may allocate before passing `std::string` values to Trace.

`TraceStorageMemory::Internal` is the deterministic default. Recent, realtime, and pending queues use internal-capable memory.

`TraceStorageMemory::PreferPsram` uses PSRAM for recent and pending queues when PSRAM is available, otherwise it falls back to internal-capable memory. The realtime queue remains internal.

`TraceStorageMemory::RequirePsram` requires recent and pending queues to allocate in PSRAM. `init()` returns `TraceStatus::OutOfMemory` if PSRAM is unavailable or allocation fails. The realtime queue remains internal.

## Payload limits

`maxTagLength` applies to `TraceLog::tag`.

`maxMessageLength` applies to direct message logging APIs.

`maxFormattedLength` applies to `printf`-style and JSON-formatted input before it becomes `TraceLog::message`.

Runtime payload limits are bounded by compile-time caps:

```cpp
TRACE_RECORD_MAX_TAG_LENGTH
TRACE_RECORD_MAX_MESSAGE_LENGTH
TRACE_FORMATTED_BUFFER_LENGTH
TRACE_TIME_TEXT_BUFFER_LENGTH
```

Setting a runtime limit above the compiled cap clamps to the compiled cap. Setting a runtime payload limit to `0` uses the compiled cap.

When Trace truncates a log, `TraceLog::truncated` is set. `TraceDiag::truncatedLogCount` increments once per log record, even if both tag and message were truncated.

## Flush triggers

Trace flushes pending logs when:

* `flush()` or `flushAndWait()` requests it.
* `pendingLogCount >= flushEveryLogs`.
* `flushIntervalMs` elapses with pending logs.
* `flushOnError` is enabled and an `Error` or `Fatal` log is queued.

`retryIntervalMs` controls the delay after `TraceFlushResult::Retry`. Values smaller than the worker poll interval are clamped so retry cannot spin in a tight loop. Normal flush requests set `flushRequested` but do not clear or bypass the retry deadline. Urgent error and fatal flush requests may bypass the retry deadline.

## Flush results

`TraceFlushResult::Ok` removes flushed pending logs.

`TraceFlushResult::Failed` keeps pending logs, increments failure diagnostics, and makes `flushAndWait()` return `TraceStatus::FlushFailed`.

`TraceFlushResult::Retry` keeps pending logs, increments retry diagnostics, schedules the next attempt with `retryIntervalMs`, and keeps `flushAndWait()` waiting until success, failure, or timeout.

## Overflow policies

`DropOldestPending` removes the oldest pending log and queues the new log.

`DropNewest` keeps the recent log but does not queue the new log for flush.

`BlockCaller` requests a flush and waits up to `blockCallerTimeoutMs` for pending space.

`FlushImmediately` requests a flush immediately and queues the new record only if pending space becomes available before `blockCallerTimeoutMs` expires.

## Stack policy

Stack sizes are ESP32 FreeRTOS byte sizes and must be at least 1024 bytes.

`TraceStackType::Auto` prefers PSRAM task stacks when supported and falls back to internal RAM.

`TraceStackType::Internal` forces normal task creation.

`TraceStackType::Psram` requires PSRAM task-stack support and fails clearly when unavailable.

## Stream colors

`enableColors` controls ANSI color codes for `setStream()` output. It defaults to `true`.

Callbacks, flush batches, and query helpers keep `TraceLog::formatted` as plain text even when stream colors are enabled.

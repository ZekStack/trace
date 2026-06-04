# Configuration

`TraceConfig` controls the internal task, log limits, flush timing, overflow behavior, JSON formatting, and level filtering.

```cpp
TraceConfig config;
config.stackSize = 4096;
config.priority = 1;
config.coreId = tskNO_AFFINITY;
config.stackType = TraceStackType::Auto;
config.maxRecentLogs = 100;
config.maxPendingLogs = 50;
config.flushEveryLogs = 20;
config.flushIntervalMs = 30000;
config.flushOnError = true;
config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
config.jsonFormat = TraceJsonFormat::Compact;
config.minLevel = TraceLevel::Debug;
config.blockCallerTimeoutMs = 1000;
```

## Buffers

`maxRecentLogs` controls queryable in-RAM history. Recent logs stay available until overwritten by newer logs.

`maxPendingLogs` controls unsaved logs waiting for `onFlush()`. Pending logs are cleared only after the flush callback returns `TraceFlushResult::Ok`.

## Flush triggers

Trace flushes pending logs when:

* `flush()` or `flushAndWait()` requests it.
* `pendingLogCount >= flushEveryLogs`.
* `flushIntervalMs` elapses with pending logs.
* `flushOnError` is enabled and an `Error` or `Fatal` log is queued.

## Overflow policies

`DropOldestPending` removes the oldest pending log and queues the new log.

`DropNewest` keeps the recent log but does not queue the new log for flush.

`BlockCaller` requests a flush and waits up to `blockCallerTimeoutMs` for pending space.

`FlushImmediately` requests a flush immediately and retries until space appears or `blockCallerTimeoutMs` expires.

## Stack policy

Stack sizes are ESP32 FreeRTOS byte sizes and must be at least 1024 bytes.

`TraceStackType::Auto` prefers PSRAM task stacks when supported and falls back to internal RAM.

`TraceStackType::Internal` forces normal task creation.

`TraceStackType::Psram` requires PSRAM task-stack support and fails clearly when unavailable.

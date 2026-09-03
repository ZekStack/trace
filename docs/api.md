# API

## Result types

`TraceResult` contains:

| Field | Type | Description |
| --- | --- | --- |
| `result` | `bool` | `true` on success. |
| `status` | `TraceStatus` | Machine-readable status. |
| `message` | `const char *` | Static human-readable status text. |

Trace-owned result messages are literals and do not require dynamic string ownership.

`TraceFlushResult` values are `Ok`, `Failed`, and `Retry`.

## TraceConfig memory API

Trace v0.3.0 uses Strata directly:

```cpp
struct TraceConfig {
	Strata::MemoryPolicy memory{
		.allocation = Strata::Placement::PreferExternal,
		.taskStack = Strata::Placement::PreferExternal,
	};
	std::optional<Strata::Placement> realtimeAllocation{};
	// ...
};
```

`realtimeAllocation == std::nullopt` means inherit `memory.allocation`.

The old `TraceStackType` and `TraceStorageMemory` enums were removed in v0.3.0.

## Main methods

```cpp
TraceResult init(const TraceConfig &config = TraceConfig());
TraceResult end(uint32_t timeoutMs = 5000);

void onFlush(TraceFlushCallback callback);
void onLog(TraceLogCallback callback);
void setStream(Print *stream);
Print *getStream();

TraceResult attachTempo(Tempo &tempo, const TraceTempoConfig &config = TraceTempoConfig());
void detachTempo();

TraceResult debug(const char *tag, const char *message);
TraceResult info(const char *tag, const char *message);
TraceResult warn(const char *tag, const char *message);
TraceResult error(const char *tag, const char *message);
TraceResult fatal(const char *tag, const char *message);

TraceResult debug(const char *tag, const char *format, Args... args);
TraceResult info(const char *tag, const char *format, Args... args);
TraceResult warn(const char *tag, const char *format, Args... args);
TraceResult error(const char *tag, const char *format, Args... args);
TraceResult fatal(const char *tag, const char *format, Args... args);

TraceResult debug(const char *tag, const JsonDocument &doc);
TraceResult info(const char *tag, const JsonDocument &doc);
TraceResult warn(const char *tag, const JsonDocument &doc);
TraceResult error(const char *tag, const JsonDocument &doc);
TraceResult fatal(const char *tag, const JsonDocument &doc);

TraceResult flush();
TraceResult flushAndWait(uint32_t timeoutMs);
```

The `std::string` message overloads remain accepted as caller-owned input. Trace copies bounded content into its fixed internal record instead of taking ownership of the caller's string.

## TraceLog

`TraceLog` stores:

```cpp
uint64_t sequence;
TraceLevel level;
Strata::String tag;
Strata::String message;
Strata::String formatted;
Strata::String timeText;
uint64_t uptimeMs;
bool truncated;
```

`TraceLog` accepts an optional `Strata::Placement` constructor argument. Trace itself creates logs with the correct resolved placement for the operation.

Without Tempo, formatted output is:

```txt
[L][TAG] - Message
```

With Tempo:

```txt
[L][TAG](time) - Message
```

## TraceLogList and queries

```cpp
using TraceLogList = Strata::Vector<TraceLog>;

TraceDiag getDiagnostics();
TraceLog getLastLog();
TraceLogList getLogs();
TraceLogList getLogs(TraceLevel level);
TraceLogList getLastLogs(size_t count);
TraceLogList getLogsByTag(const char *tag);
```

Query list backing storage and each returned log's owned strings follow `TraceConfig::memory.allocation`.

## TraceLogBatch

```cpp
struct TraceLogBatch {
	TraceLogList logs;
	uint64_t createdAtUptimeMs;
};
```

Flush batch storage follows `TraceConfig::memory.allocation`. `maxFlushBatchLogs` can cap the number of public logs converted per callback invocation.

## Diagnostics

`TraceDiag` includes queue counts, drop/flush counters, stack high-water mark, fixed queue allocation sizes, and Strata placement information.

Memory-related fields are:

```cpp
Strata::Placement requestedAllocationPlacement;
Strata::Placement requestedRealtimeAllocationPlacement;
Strata::Placement requestedTaskStackPlacement;
Strata::Region taskStackRegion;

size_t recentAllocatedBytes;
size_t realtimeAllocatedBytes;
size_t pendingAllocatedBytes;
Strata::Region recentStorageRegion;
Strata::Region realtimeStorageRegion;
Strata::Region pendingStorageRegion;
```

Use `Strata::toString()` to present `Placement` and `Region` values.

A `PreferExternal` request may report `Internal` when Strata fell back because external memory was unavailable. `RequireExternal` never falls back.

## Callbacks

```cpp
using TraceFlushCallback = std::function<TraceFlushResult(const TraceLogBatch &)>;
using TraceLogCallback = std::function<void(const TraceLog &)>;
```

Trace stores callback holders through Strata-backed shared ownership. The flush callback holder follows the general allocation policy; the realtime callback holder follows the resolved realtime allocation policy.

Allocations already owned by the caller's lambda/capture before it is passed to Trace remain caller-owned.

Both callbacks run from the internal Trace task. Avoid long blocking work and do not recursively log through the same Trace instance.

## Stream output

`setStream()` writes formatted realtime logs to any Arduino `Print` stream. Trace does not own the stream.

```cpp
trace.setStream(&Serial);
trace.setStream(&Serial1);
trace.setStream(&client);
trace.setStream(nullptr);
```

Keep the stream alive until `Trace::end()` completes.

## Shutdown ownership

The internal worker is a `Strata::FreeRTOS::Task`. The task does not self-delete. On shutdown it completes its work, publishes that it is ready for deletion, and suspends. `Trace::end()` then resets the Strata task from the caller context before releasing buffers.

If a timed `end()` returns before the worker is ready, ownership is retained so cleanup can be retried. The destructor performs an unbounded `end()` to prevent the task from outliving the Trace implementation.

## Tempo lifetime

Trace does not own an attached `Tempo`. Keep it alive until `Trace::end()` completes.

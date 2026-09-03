# Trace

Trace is a logging and diagnostics library for ESP32.

Trace collects structured runtime logs with bounded recent history, bounded realtime delivery, bounded pending flush storage, task-side persistence callbacks, optional Tempo timestamps, and runtime diagnostics. Trace v0.3.0 uses [Strata](https://github.com/ZekStack/strata) for all movable library-owned memory and FreeRTOS storage.

[![CI](https://github.com/ZekStack/trace/actions/workflows/ci.yml/badge.svg)](https://github.com/ZekStack/trace/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ZekStack/trace?sort=semver)](https://github.com/ZekStack/trace/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Why use Trace?

* **Bounded storage** - recent history, realtime delivery, pending flush logs, and payload lengths use fixed-capacity storage.
* **PSRAM-first defaults** - movable Trace-owned allocations and the Trace task stack prefer external memory by default and fall back to internal memory when needed.
* **Shared ZekStack memory policy** - allocation and task placement use `Strata::MemoryPolicy`, `Strata::Placement`, and `Strata::Region`.
* **Structured output** - log records keep level, tag, message, formatted text, sequence, and uptime.
* **Task-side callbacks** - realtime observation and persistence callbacks run from the internal Trace task.
* **Production-minded** - result-based errors, diagnostics, thread-safe internals, ArduinoJson support, and no exceptions in Trace production sources.

## Dependencies

Trace v0.3.0 requires:

* ArduinoJson v7 or newer.
* Strata v0.1.2.
* C++20.

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps =
  https://github.com/ZekStack/trace.git#v0.3.0
  https://github.com/ZekStack/strata.git#v0.1.2
  bblanchon/ArduinoJson@>=7.0.0

build_flags =
  -std=gnu++20
build_unflags =
  -std=gnu++11
```

### Arduino IDE

Trace and Strata are not published to Arduino Library Manager yet. Install both repositories into your Arduino libraries folder and install ArduinoJson through Library Manager.

```txt
Arduino/libraries/Trace
Arduino/libraries/Strata
```

## Quick start

```cpp
#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	trace.setStream(&Serial);

	TraceResult result = trace.init();
	if (!result) {
		Serial.println(result.message);
		return;
	}

	trace.info("BOOT", "Trace is ready");
}

void loop() {
	delay(1000);
}
```

With the default configuration, all movable Trace-owned memory prefers PSRAM:

```cpp
TraceConfig config;

// These are already the defaults.
config.memory.allocation = Strata::Placement::PreferExternal;
config.memory.taskStack = Strata::Placement::PreferExternal;

// nullopt means: inherit memory.allocation.
config.realtimeAllocation = std::nullopt;
```

`PreferExternal` is deliberately different from `RequireExternal`: devices without usable PSRAM still work by falling back to internal memory.

If realtime conversion must stay internal while history, pending storage, flush batches, and the task stack prefer PSRAM:

```cpp
TraceConfig config;
config.realtimeAllocation = Strata::Placement::Internal;
trace.init(config);
```

## Memory model

`TraceConfig::memory.allocation` controls movable general Trace-owned allocations, including recent and pending ring storage, public query values, flush batches, formatted strings, and general callback ownership.

`TraceConfig::memory.taskStack` controls the Trace worker task stack.

`TraceConfig::realtimeAllocation` optionally overrides the general allocation placement for realtime queue storage, realtime callback ownership, and realtime `TraceLog` conversion. When it is `std::nullopt`, realtime memory inherits `memory.allocation`.

Strata keeps RTOS control structures internal where required by the platform. Trace does not override those safety constraints.

Diagnostics report both what was requested and where memory actually landed:

```cpp
TraceDiag diag = trace.getDiagnostics();

Serial.printf("allocation requested: %s\n", Strata::toString(diag.requestedAllocationPlacement));
Serial.printf("recent region: %s\n", Strata::toString(diag.recentStorageRegion));
Serial.printf("task stack region: %s\n", Strata::toString(diag.taskStackRegion));
```

## Allocation behavior

The internal queue records remain fixed-size `TraceRecord` values. After `Trace::init()`, accepted direct C-string log calls do not allocate on the internal enqueue path while target queues have capacity and no output-boundary conversion is triggered.

Public/output-boundary values use Strata-backed storage:

* `TraceLog` strings are `Strata::String`.
* `TraceLogList` is `Strata::Vector<TraceLog>`.
* `TraceLogBatch::logs` is a `TraceLogList`.
* query values and flush batches follow `memory.allocation`.
* realtime conversion follows `realtimeAllocation` or the inherited general policy.

Caller-owned allocations, such as captures created before a callback is passed to Trace or caller-created `std::string` values, remain the caller's responsibility.

## Important notes

> [!IMPORTANT]
> `info()`, `debug()`, `warn()`, `error()`, and `fatal()` only enqueue logs. `onLog()` and `onFlush()` callbacks run later from the internal Trace task.

* `maxRecentLogs` controls queryable in-RAM history only.
* `maxRealtimeLogs` controls realtime delivery used by `onLog()` and stream output.
* `maxPendingLogs` controls unsaved logs waiting for flush.
* `maxFlushBatchLogs` bounds the number of public `TraceLog` objects converted per flush callback. `0` keeps flush batches uncapped.
* Queue-count `0` disables that queue and allocates no ring storage for it.
* `setStream()` writes formatted realtime logs to any Arduino `Print` implementation.
* Callbacks should avoid long blocking work and should not recursively call Trace logging methods.
* Trace does not own attached `Print` or `Tempo` instances. Keep them alive until `Trace::end()` completes.
* Stack sizes are FreeRTOS byte sizes on ESP32 and must be at least 1024 bytes.

## API overview

```cpp
Trace trace;
trace.init();
trace.setStream(&Serial);

trace.onLog([](const TraceLog &log) {});
trace.onFlush([](const TraceLogBatch &batch) {
	return TraceFlushResult::Ok;
});

trace.info("WIFI", "connected");
trace.errorf("HTTP", "status=%d", 500);

TraceDiag diag = trace.getDiagnostics();
TraceLogList errors = trace.getLogs(TraceLevel::Error);
trace.flushAndWait(2000);
```

## v0.2.x to v0.3.0 migration

Trace v0.3.0 intentionally adopts the shared ZekStack Strata API instead of retaining library-specific memory enums.

| v0.2.x | v0.3.0 |
| --- | --- |
| `TraceStackType::Auto` | `Strata::Placement::PreferExternal` |
| `TraceStackType::Internal` | `Strata::Placement::Internal` |
| `TraceStackType::Psram` | `Strata::Placement::RequireExternal` |
| `TraceStorageMemory::Internal` | `Strata::Placement::Internal` |
| `TraceStorageMemory::PreferPsram` | `Strata::Placement::PreferExternal` |
| `TraceStorageMemory::RequirePsram` | `Strata::Placement::RequireExternal` |
| `config.stackType` | `config.memory.taskStack` |
| `config.storageMemory` | `config.memory.allocation` |
| `config.realtimeStorageMemory` | `config.realtimeAllocation` |
| `std::vector<TraceLog>` query results | `TraceLogList` |
| `TraceResult::message` as `std::string` | `const char *` |

The default storage policy also changes: v0.3.0 prefers external memory for movable Trace-owned allocations and the task stack.

## Examples

| Example | Description |
| --- | --- |
| `Basic` | Minimal init, realtime log printing, and manual flush. |
| `JsonPayloads` | Compact and pretty ArduinoJson payload logging. |
| `PrintfFormatting` | `printf`-style logging helpers. |
| `CallbacksAndFlush` | Realtime observation and persistence callback behavior. |
| `Diagnostics` | Runtime counters, requested placements, and observed regions. |
| `TempoTimestamps` | Tempo timestamp formatting. |
| `OverflowPolicies` | Pending queue limits and overflow policy configuration. |

## Documentation

* [`docs/getting-started.md`](docs/getting-started.md)
* [`docs/configuration.md`](docs/configuration.md)
* [`docs/api.md`](docs/api.md)
* [`docs/examples.md`](docs/examples.md)
* [`docs/troubleshooting.md`](docs/troubleshooting.md)

## Compatibility

| Item | Support |
| --- | --- |
| Framework | Arduino ESP32 |
| Platform | `espressif32` |
| Language | C++20 |
| PSRAM | Optional; preferred by default for movable Trace-owned memory |
| Dependencies | ArduinoJson >= 7.0.0, Strata v0.1.2 |
| Exceptions in Trace production sources | Not used |
| Status | v0.3.0 |

## License

MIT - see [`LICENSE.md`](LICENSE.md).

## ZekStack

Part of the ZekStack ESP32 library stack.

# Trace

Trace is a logging and diagnostics library for ESP32.

Trace helps you collect structured runtime logs in Arduino ESP32 projects with bounded RAM history, bounded realtime delivery, bounded pending flush storage, task-side persistence callbacks, optional Tempo timestamps, and diagnostics. It is designed for products that need predictable logging behavior without relying on ESP-IDF or Arduino logging macros.

[![CI](https://github.com/ZekStack/trace/actions/workflows/ci.yml/badge.svg)](https://github.com/ZekStack/trace/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ZekStack/trace?sort=semver)](https://github.com/ZekStack/trace/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Why use Trace?

* **Bounded memory** - recent history, realtime delivery, pending flush logs, and payload lengths use fixed-capacity storage.
* **Structured output** - log records keep level, tag, message, formatted text, sequence, and uptime.
* **Task-side callbacks** - realtime observation and persistence callbacks run from the internal Trace task.
* **ESP32 task control** - configure byte stack size, priority, core affinity, and stack memory preference.
* **Production-minded** - result-based errors, diagnostics, thread-safe internals, ArduinoJson support, and no exceptions.

## Install

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps =
  https://github.com/ZekStack/trace.git
  bblanchon/ArduinoJson@>=7.0.0

build_flags =
  -std=gnu++20
build_unflags =
  -std=gnu++11
```

### Arduino IDE

Trace is not published to Arduino Library Manager yet.

Install it by downloading the repository ZIP or cloning it into your Arduino libraries folder.

```txt
Arduino/libraries/Trace
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
		Serial.println(result.message.c_str());
		return;
	}

	trace.info("BOOT", "Trace is ready");
}

void loop() {
	delay(1000);
}
```

## Important notes

> [!IMPORTANT]
> `info()`, `debug()`, `warn()`, `error()`, and `fatal()` only enqueue logs. `onLog()` and `onFlush()` callbacks run later from the internal Trace task.

* `maxRecentLogs` controls queryable in-RAM history only.
* `maxRealtimeLogs` controls the realtime delivery queue used by `onLog()` and stream output.
* `maxPendingLogs` controls unsaved logs waiting for flush.
* Queue-count `0` disables that queue. Payload-cap `0` uses the compiled maximum for that payload type.
* `setStream()` writes formatted realtime logs to any Arduino `Print` stream such as `Serial`, `Serial1`, `WiFiClient`, or a custom sink.
* Stream output uses ANSI colors by default. Callback, flush, and query `TraceLog::formatted` values stay plain text.
* `onLog()` is for realtime observation; `onFlush()` is for persistence.
* Callbacks should avoid long blocking work and should not recursively call Trace logging methods.
* Trace does not own attached `Print` or `Tempo` instances. Keep them alive until `Trace::end()` completes.
* Detaching or replacing `Print` or `Tempo` while Trace is active does not synchronize already snapshotted worker use.
* Stack sizes are FreeRTOS byte sizes on ESP32 and must be at least 1024 bytes.
* `TraceStackType::Auto` prefers PSRAM task stacks when supported and falls back to internal RAM.
* `TraceStorageMemory::PreferPsram` opts recent and pending log buffers into PSRAM with internal fallback. Realtime delivery stays internal.

## Examples

| Example | Description |
| --- | --- |
| `Basic` | Minimal init, realtime log printing, and manual flush. |
| `JsonPayloads` | Compact and pretty ArduinoJson payload logging. |
| `PrintfFormatting` | `printf`-style logging helpers. |
| `CallbacksAndFlush` | Realtime observation and persistence callback behavior. |
| `Diagnostics` | Runtime counters and query helpers. |
| `TempoTimestamps` | Full, minimal, and custom Tempo timestamp formatting. |
| `OverflowPolicies` | Pending queue limits and overflow policy configuration. |

Start with:

```txt
examples/Basic
```

## Documentation

Detailed documentation is available in the `docs/` folder.

| Document | Description |
| --- | --- |
| [`docs/getting-started.md`](docs/getting-started.md) | Step-by-step setup and first log flow. |
| [`docs/configuration.md`](docs/configuration.md) | Config options, queue limits, flushing, and stack behavior. |
| [`docs/api.md`](docs/api.md) | Public classes, result types, callbacks, and diagnostics. |
| [`docs/examples.md`](docs/examples.md) | Explanation of all included examples. |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | Common issues and solutions. |

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
std::vector<TraceLog> errors = trace.getLogs(TraceLevel::Error);
trace.flushAndWait(2000);
```

For the full API, see [`docs/api.md`](docs/api.md).

## Compatibility

| Item | Support |
| --- | --- |
| Framework | Arduino ESP32 |
| Platform | `espressif32` |
| Language | C++20 |
| Filesystem | none |
| PSRAM | Optional for task stacks and opt-in recent/pending log storage |
| Dependencies | `bblanchon/ArduinoJson >= 7.0.0` |
| Exceptions | Not used |
| Status | Release candidate `0.2.0-rc.1` |

## Configuration

```cpp
TraceConfig config;
config.stackSize = 4096;
config.storageMemory = TraceStorageMemory::Internal;
config.maxRecentLogs = 100;
config.maxRealtimeLogs = 100;
config.maxPendingLogs = 50;
config.flushEveryLogs = 20;
config.flushIntervalMs = 30000;
config.retryIntervalMs = 1000;
config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
config.enableColors = true;
config.maxTagLength = 32;
config.maxMessageLength = 256;
config.maxFormattedLength = 384;

TraceResult result = trace.init(config);
```

For all options, see [`docs/configuration.md`](docs/configuration.md).

## Error handling

Trace reports operation status through `TraceResult`.

```cpp
TraceResult result = trace.flushAndWait(2000);

if (!result) {
	Serial.println(result.message.c_str());
	return;
}
```

For result fields and status codes, see [`docs/api.md`](docs/api.md).

## License

MIT - see [`LICENSE.md`](LICENSE.md).

## ZekStack

Part of the ZekStack ESP32 library stack.

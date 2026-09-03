# Getting started

Trace v0.3.0 is built for Arduino ESP32, C++20, ArduinoJson v7, and Strata v0.1.2.

## Install with PlatformIO

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

## Minimal setup

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

	trace.info("BOOT", "Trace initialized");
}

void loop() {
	delay(1000);
}
```

The default memory policy prefers external memory for movable Trace-owned allocations and for the worker task stack:

```cpp
TraceConfig config;
config.memory.allocation = Strata::Placement::PreferExternal;
config.memory.taskStack = Strata::Placement::PreferExternal;
```

Those assignments are optional because they are already the defaults. `PreferExternal` falls back to internal memory when external memory is unavailable.

Realtime allocations inherit the general allocation policy by default. Override them only when required:

```cpp
TraceConfig config;
config.realtimeAllocation = Strata::Placement::Internal;
trace.init(config);
```

## Persistence

Use `onFlush()` to persist pending logs to Fresh, LittleFS, a backend, or another store.

```cpp
trace.onFlush([](const TraceLogBatch &batch) {
	for (const TraceLog &log : batch.logs) {
		Serial.println(log.formatted.c_str());
	}
	return TraceFlushResult::Ok;
});
```

`flush()` requests a flush and returns immediately. `flushAndWait()` waits until the task-side flush succeeds, fails, or the timeout expires.

## Queries

Query methods return `TraceLogList`, a Strata-backed vector:

```cpp
TraceLogList errors = trace.getLogs(TraceLevel::Error);
for (const TraceLog &log : errors) {
	Serial.println(log.formatted.c_str());
}
```

Trace creates query results using `config.memory.allocation`.

## Diagnostics

```cpp
TraceDiag diag = trace.getDiagnostics();
Serial.printf("requested=%s\n", Strata::toString(diag.requestedAllocationPlacement));
Serial.printf("recent=%s\n", Strata::toString(diag.recentStorageRegion));
Serial.printf("task=%s\n", Strata::toString(diag.taskStackRegion));
```

A requested placement and observed region are intentionally separate. For example, `PreferExternal` may resolve to internal memory when PSRAM is unavailable.

## Lifetime

`setStream()` accepts any Arduino `Print` implementation. Trace does not own the stream or an attached Tempo instance; keep them alive until `Trace::end()` completes.

The Trace task is Strata-owned static FreeRTOS storage. `end()` performs task cleanup from the caller context after the worker has finished and suspended.

# Getting started

Trace is built for Arduino ESP32 and depends on ArduinoJson v7.

## Install with PlatformIO

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

## Minimal setup

```cpp
#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);

	TraceResult result = trace.init();
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	trace.onLog([](const TraceLog &log) {
		Serial.println(log.formatted.c_str());
	});

	trace.info("BOOT", "Trace initialized");
}

void loop() {
	delay(1000);
}
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

`flush()` requests a flush and returns immediately. `flushAndWait()` waits until the task-side flush finishes or the timeout expires.

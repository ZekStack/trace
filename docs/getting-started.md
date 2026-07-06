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
	trace.setStream(&Serial);

	TraceResult result = trace.init();
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	trace.info("BOOT", "Trace initialized");
}

void loop() {
	delay(1000);
}
```

`setStream()` accepts any Arduino `Print` implementation. Use `trace.setStream(&Serial1)`,
`trace.setStream(&client)`, or `trace.setStream(nullptr)` to change or disable stream output.

Trace does not own the stream. Keep it alive until `Trace::end()` completes.

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

`flush()` requests a flush and returns immediately. `flushAndWait()` waits until the task-side flush succeeds, fails, or the timeout expires. A retry result schedules another attempt and keeps `flushAndWait()` waiting within its timeout.

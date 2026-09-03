#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceConfig config;
	config.flushEveryLogs = 3;
	config.flushIntervalMs = 10000;

	TraceResult result = trace.init(config);
	if (!result) {
		Serial.println(result.message);
		return;
	}

	trace.onLog([](const TraceLog &log) {
		Serial.printf("realtime: %s\n", log.formatted.c_str());
	});

	trace.onFlush([](const TraceLogBatch &batch) {
		Serial.printf("persisting batch: %u logs\n", static_cast<unsigned>(batch.size()));
		for (const TraceLog &log : batch.logs) {
			Serial.println(log.formatted.c_str());
		}
		return TraceFlushResult::Ok;
	});

	trace.info("APP", "first");
	trace.info("APP", "second");
	trace.info("APP", "third");
}

void loop() {
	delay(1000);
}

#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceResult result = trace.init();
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	trace.onLog([](const TraceLog &log) {
		Serial.println(log.formatted.c_str());
	});

	trace.onFlush([](const TraceLogBatch &batch) {
		Serial.printf("flushing %u logs\n", static_cast<unsigned>(batch.size()));
		return TraceFlushResult::Ok;
	});

	trace.info("BOOT", "Trace started");
	trace.warn("BOOT", "Manual flush requested");
	trace.flush();
}

void loop() {
	delay(1000);
}

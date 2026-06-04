#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceConfig config;
	config.maxRecentLogs = 10;
	config.maxPendingLogs = 3;
	config.flushEveryLogs = 100;
	config.flushIntervalMs = 0;
	config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;

	TraceResult result = trace.init(config);
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	trace.onFlush([](const TraceLogBatch &batch) {
		Serial.printf("flush batch=%u\n", static_cast<unsigned>(batch.size()));
		return TraceFlushResult::Ok;
	});

	for (int i = 0; i < 8; ++i) {
		trace.info("OVERFLOW", "pending item=%d", i);
	}

	TraceDiag diag = trace.getDiagnostics();
	Serial.printf("recent=%u pending=%u dropped=%u\n",
	    static_cast<unsigned>(diag.recentLogCount),
	    static_cast<unsigned>(diag.pendingLogCount),
	    static_cast<unsigned>(diag.droppedLogCount));

	trace.flushAndWait(2000);
}

void loop() {
	delay(1000);
}

#include <Arduino.h>
#include <Trace.h>

Trace trace;

void printDiagnostics() {
	TraceDiag diag = trace.getDiagnostics();
	Serial.printf("recent=%u\n", static_cast<unsigned>(diag.recentLogCount));
	Serial.printf("pending=%u\n", static_cast<unsigned>(diag.pendingLogCount));
	Serial.printf("dropped=%u\n", static_cast<unsigned>(diag.droppedLogCount));
	Serial.printf("flushOk=%u\n", static_cast<unsigned>(diag.flushSuccessCount));
	Serial.printf("flushFail=%u\n", static_cast<unsigned>(diag.flushFailCount));
}

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceConfig config;
	config.maxRecentLogs = 5;
	config.maxPendingLogs = 5;

	if (!trace.init(config)) {
		return;
	}

	trace.info("APP", "one");
	trace.warn("APP", "two");
	trace.error("NET", "three");

	TraceLog last = trace.getLastLog();
	Serial.println(last.formatted.c_str());

	std::vector<TraceLog> appLogs = trace.getLogsByTag("APP");
	Serial.printf("APP logs=%u\n", static_cast<unsigned>(appLogs.size()));

	printDiagnostics();
}

void loop() {
	delay(1000);
}

#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	if (!trace.init()) {
		return;
	}

	trace.onLog([](const TraceLog &log) {
		Serial.println(log.formatted.c_str());
	});

	int retryCount = 3;
	std::string status = "connecting";

	trace.info("WIFI", "status=%s retries=%d", status.c_str(), retryCount);
	trace.errorf("HTTP", "request failed status=%d", 500);
}

void loop() {
	delay(1000);
}

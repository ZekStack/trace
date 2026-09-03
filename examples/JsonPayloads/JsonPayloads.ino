#include <Arduino.h>
#include <ArduinoJson.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceConfig config;
	config.jsonFormat = TraceJsonFormat::Pretty;

	TraceResult result = trace.init(config);
	if (!result) {
		Serial.println(result.message);
		return;
	}

	trace.onLog([](const TraceLog &log) {
		Serial.println(log.formatted.c_str());
	});

	JsonDocument doc;
	doc["sensor"] = "garage";
	doc["temperature"] = 23.5;
	doc["online"] = true;

	trace.info("SENSOR", doc);
}

void loop() {
	delay(1000);
}

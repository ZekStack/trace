#include <Arduino.h>
#include <Tempo.h>
#include <Trace.h>

Trace trace;
Tempo tempo;

bool customTraceTime(const Tempo &tempoRef, char *buffer, size_t bufferSize) {
	LocalDateTime now = tempoRef.nowLocal();
	if (!now.ok) {
		return false;
	}
	snprintf(
	    buffer,
	    bufferSize,
	    "%04d/%02d/%02d %02d:%02d:%02d",
	    now.year,
	    now.month,
	    now.day,
	    now.hour,
	    now.minute,
	    now.second
	);
	return true;
}

void setup() {
	Serial.begin(115200);
	delay(200);

	TempoConfig tempoConfig;
	tempoConfig.timeZone = "UTC0";
	tempo.init(tempoConfig);

	trace.init();
	trace.onLog([](const TraceLog &log) {
		Serial.println(log.formatted.c_str());
	});

	TraceTempoConfig traceTime;
	traceTime.format = TraceTimeFormat::Full;
	trace.attachTempo(tempo, traceTime);
	trace.info("TIME", "full timestamp");

	traceTime.format = TraceTimeFormat::Minimal;
	trace.attachTempo(tempo, traceTime);
	trace.info("TIME", "minimal timestamp");

	traceTime.format = TraceTimeFormat::Custom;
	traceTime.formatter = customTraceTime;
	trace.attachTempo(tempo, traceTime);
	trace.info("TIME", "custom timestamp");
}

void loop() {
	delay(1000);
}

#include <Arduino.h>
#include <Trace.h>

Trace trace;

void setup() {
	Serial.begin(115200);
	delay(200);

	TraceConfig config;
	config.memory.allocation = Strata::Placement::PreferExternal;
	config.memory.taskStack = Strata::Placement::PreferExternal;
	config.realtimeAllocation = Strata::Placement::Internal;

	TraceResult result = trace.init(config);
	if (!result) {
		Serial.println(result.message);
		return;
	}

	TraceDiag diag = trace.getDiagnostics();
	Serial.printf("general requested: %s\n", Strata::toString(diag.requestedAllocationPlacement));
	Serial.printf("realtime requested: %s\n", Strata::toString(diag.requestedRealtimeAllocationPlacement));
	Serial.printf("task requested: %s\n", Strata::toString(diag.requestedTaskStackPlacement));
	Serial.printf("recent region: %s\n", Strata::toString(diag.recentStorageRegion));
	Serial.printf("realtime region: %s\n", Strata::toString(diag.realtimeStorageRegion));
	Serial.printf("task region: %s\n", Strata::toString(diag.taskStackRegion));

	trace.info("MEM", "general storage prefers PSRAM; realtime stays internal");
}

void loop() {
	delay(1000);
}

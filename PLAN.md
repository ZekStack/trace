Trace

A logging and diagnostics library for ESP32.

Trace provides structured logging, debug traces, warnings, errors, persistent logs, and diagnostic output.



Rules

Sync / flush will always be called from the internal free rtos task

No throw, no exceptions

We use our own logging. No ESP IDF LOGx macros or Arduino logging.

Trace stores recent logs and pending logs separately.

maxRecentLogs controls the queryable in-RAM history.

maxPendingLogs controls unsaved logs waiting for flush.

Trace never stores more than these configured limits.

Example usage

#include <Trace.h>

Trace trace;
void setup(){
	TraceConfig config;
	config.stackSize = 4096;
	config.coreId = tskNO_AFFINITY;
	config.priority = 1;
	config.stackType = TraceStackType::Auto; // Auto, Internal, Psram
	config.maxRecentLogs = 100;        // queryable RAM history
	config.maxPendingLogs = 50;        // unsaved logs waiting for flush

	config.flushEveryLogs = 20;        // flush after 20 pending logs
	config.flushIntervalMs = 30000;    // or every 30 seconds
	config.flushOnError = true;        // flush quickly after error/fatal logs

	config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
	config.jsonFormat = TraceJsonFormat::Compact; // TraceJsonFormat::Pretty;
	config.minLevel = TraceLevel::Debug;
	
	TraceResult result = trace.init(config);
	if( !result ) { Serial.printf("Trace failed to initialize. Error: %s", result.message); return; }
	
	trace.onFlush([](const TraceLogBatch& batch) -> TraceFlushResult {
    	// Save logs to Fresh, LittleFS, backend, etc.
    	return TraceFlushResult::Ok;
	});
	
	/*
		onLog is for realtime observation, not persistence.
		onFlush is for persistence.
		Both callbacks run from the internal Trace task.
		Callbacks must not block for a long time.
		Callbacks should not call trace.info/debug/warn/error recursively.
	*/
	trace.onLog([](const TraceLog& log){
		// the app just used trace for logging, we can send that to websockets or anywhere else
	});
	
	trace.info("SOME_TAG","Just an info log!");
	trace.debug("SOME_TAG","Just an debug log!");
	trace.error("SOME_TAG","Just an error log!");
	trace.warn("SOME_TAG","Just an warning log!");
	
	// We must support ArduinoJson v7 and up and use SerializeJsonPretty for printing
	JsonDocument doc;
	doc["hello"] = "world";
	trace.info("SOME_TAG",doc);
	trace.debug("SOME_TAG",doc);
	trace.error("SOME_TAG",doc);
	trace.warn("SOME_TAG",doc);
	
	// we must support c style printing as well
	int exampleInt = 5;
	trace.info("SOME_TAG","Example var: %d", exampleInt);
	trace.debug("SOME_TAG","Example var: %d", exampleInt);
	trace.error("SOME_TAG","Example var: %d", exampleInt);
	trace.warn("SOME_TAG","Example var: %d", exampleInt);
	
	std::string exampleStr = "Hello!";
	trace.info("SOME_TAG","Example var: %s", exampleStr.c_str());
	trace.debug("SOME_TAG","Example var: %s", exampleStr.c_str());
	trace.error("SOME_TAG","Example var: %s", exampleStr.c_str());
	trace.warn("SOME_TAG","Example var: %s", exampleStr.c_str());
	
	TraceDiag diag = trace.getDiagnostics();

	/*
		diag.recentLogCount;
		diag.pendingLogCount;
		diag.droppedLogCount;
		diag.flushSuccessCount;
		diag.flushFailCount;
		diag.lastFlushAtMs;
	*/
}

Other methods

TraceLog latestLog = trace.getLastLog();
std::vector<TraceLog> logs = trace.getLogs();
std::vector<TraceLog> logs = trace.getLogs(TraceLevel::Error);
std::vector<TraceLog> logs = trace.getLastLogs(20);
std::vector<TraceLog> logs = trace.getLogsByTag("WIFI");
TraceResult trace.flush();
TraceResult trace.flushAndWait(uint32_t timeoutMs);

/*
	flush() requests a flush from the internal Trace task and returns immediately.
	flushAndWait() requests a flush and waits until it finishes or times out.
*/



The output style must be

[LEVEL][TAG] - Message

If Tempo is added as an addon like this

#include <Trace.h>
#include <Tempo.h>

Trace trace;
Tempo tempo;
TempoScheduler scheduler;

void setup(){
	tempo.init(config);
	scheduler.init(tempo);
	
	TraceTempoConfig tempoConfig;
	tempoConfig.format = TraceTimeFormat::Full;
	trace.init(config);
	trace.attachTempo(tempo, tempoConfig);
}

The output style with tempo must be

// Full
[LEVEL][TAG](2026-06-05 15:50:22) - Message
// Minimal
[LEVEL][TAG](15:50) - Message

even without Tempo, every TraceLog should always store uptime:



uint64_t uptimeMs;

Other internals



enum class TraceOverflowPolicy {
    DropOldestPending,
    DropNewest,
    BlockCaller,
    FlushImmediately
};

enum class TraceTimeFormat {
    None,
    Full,
    Minimal,
    Iso8601,
    UnixSeconds,
    UptimeMs,
    Custom
};



Recommended rule:

trace.info/debug/warn/error only enqueue logs.
onLog and onSync callbacks are executed from the internal Trace task.

Same idea as Worker:

Auto     -> prefer PSRAM if supported, fallback to internal RAM
Internal -> force internal RAM
Psram    -> require PSRAM, fail if unavailable

And allow a custom formatter callback:



using TraceTimeFormatter = bool (*)(const Tempo& tempo, char* buffer, size_t bufferSize);



Then:



TraceTempoConfig tempoConfig;
tempoConfig.format = TraceTimeFormat::Full;
trace.attachTempo(tempo, tempoConfig);



For custom:



TraceTempoConfig tempoConfig;
tempoConfig.format = TraceTimeFormat::Custom;
tempoConfig.formatter = customTimeFormatter;
trace.attachTempo(tempo, tempoConfig);



Example:



bool customTimeFormatter(const Tempo& tempo, char* buffer, size_t bufferSize) {
    LocalDateTime now = tempo.nowLocal();
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

Recommended behavior

When a log is created:



1. Trace creates TraceLog
2. Trace stores it in recent RAM logs
3. Trace stores it in pending flush queue
4. Trace internal task flushes pending logs later
5. If flush succeeds, pending logs are cleared
6. Recent logs stay available in RAM until overwritten by newer logs
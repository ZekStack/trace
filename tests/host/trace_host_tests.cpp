#include <Trace.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

std::atomic<uint64_t> trace_host_millis{0};

namespace {
struct FakePrint : Print {
	std::string output;

	size_t write(uint8_t value) override {
		output.push_back(static_cast<char>(value));
		return 1;
	}
};

struct TestRunner {
	int failed = 0;

	void check(bool condition, const char *message) {
		if (!condition) {
			failed++;
			std::cerr << "FAIL: " << message << "\n";
		}
	}

	void check(const TraceResult &result, const char *message) {
		check(static_cast<bool>(result), message);
	}
};

void resetClock() {
	trace_host_millis.store(0);
}

bool waitUntil(const std::function<bool()> &predicate, uint32_t timeoutMs = 500) {
	const uint64_t started = trace_host_millis.load();
	while (trace_host_millis.load() - started <= timeoutMs) {
		if (predicate()) {
			return true;
		}
		vTaskDelay(1);
	}
	return predicate();
}

TraceConfig baseConfig() {
	TraceConfig config;
	config.flushIntervalMs = 0;
	config.flushEveryLogs = 0;
	config.enableColors = false;
	config.retryIntervalMs = 25;
	config.blockCallerTimeoutMs = 25;
	return config;
}

void realtimeWorksWithoutRecent(TestRunner &runner) {
	resetClock();
	Trace trace;
	FakePrint stream;
	int observed = 0;
	trace.setStream(&stream);
	trace.onLog([&observed](const TraceLog &log) {
		if (log.message == "ready") {
			observed++;
		}
	});

	TraceConfig config = baseConfig();
	config.maxRecentLogs = 0;
	runner.check(trace.init(config), "init realtimeWorksWithoutRecent");
	runner.check(trace.info("BOOT", "ready"), "log realtimeWorksWithoutRecent");
	runner.check(waitUntil([&observed]() { return observed == 1; }), "onLog fires with no recent history");
	runner.check(stream.output.find("[I][BOOT] - ready") != std::string::npos, "stream writes with no recent history");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.recentLogCount == 0, "recent history disabled");
	runner.check(diag.realtimeLogCount == 1, "realtime queued count increments");
	runner.check(trace.end(), "end realtimeWorksWithoutRecent");
}

void smallRecentDoesNotDropRealtime(TestRunner &runner) {
	resetClock();
	Trace trace;
	int observed = 0;
	trace.onLog([&observed](const TraceLog &) { observed++; });
	TraceConfig config = baseConfig();
	config.maxRecentLogs = 1;
	config.maxRealtimeLogs = 10;
	runner.check(trace.init(config), "init smallRecentDoesNotDropRealtime");
	for (int i = 0; i < 5; ++i) {
		runner.check(trace.infof("BURST", "item=%d", i), "burst log");
	}
	runner.check(waitUntil([&observed]() { return observed == 5; }), "all realtime callbacks delivered");
	runner.check(trace.getLogs().size() == 1, "recent history keeps only one");
	runner.check(trace.end(), "end smallRecentDoesNotDropRealtime");
}

void realtimeOverflowDiagnostics(TestRunner &runner) {
	resetClock();
	Trace trace;
	int observed = 0;
	trace.onLog([&observed](const TraceLog &) {
		observed++;
		vTaskDelay(1);
	});
	TraceConfig config = baseConfig();
	config.maxRealtimeLogs = 1;
	runner.check(trace.init(config), "init realtimeOverflowDiagnostics");
	for (int i = 0; i < 100; ++i) {
		runner.check(trace.infof("FAST", "item=%d", i), "fast realtime log");
	}
	runner.check(waitUntil([&trace]() { return trace.getDiagnostics().realtimeLogCount >= 100; }), "realtime queued diagnostic reaches 100");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.droppedRealtimeLogCount > 0, "realtime overflow increments drop diagnostic");
	runner.check(observed < 100, "overflow drops some callback deliveries");
	runner.check(trace.end(), "end realtimeOverflowDiagnostics");
}

void realtimeDisabledHasNoRealtimeDiagnostics(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	runner.check(trace.init(config), "init realtimeDisabledHasNoRealtimeDiagnostics");
	runner.check(trace.info("NOOUT", "message"), "log without realtime output");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.realtimeLogCount == 0, "disabled realtime does not queue");
	runner.check(diag.droppedRealtimeLogCount == 0, "disabled realtime does not drop");
	runner.check(trace.end(), "end realtimeDisabledHasNoRealtimeDiagnostics");
}

void pendingOverflowPolicies(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		std::vector<uint64_t> flushed;
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 2;
		config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
		runner.check(trace.init(config), "init DropOldestPending");
		trace.info("P", "one");
		trace.info("P", "two");
		trace.info("P", "three");
		trace.onFlush([&flushed](const TraceLogBatch &batch) {
			for (const TraceLog &log : batch.logs) {
				flushed.push_back(log.sequence);
			}
			return TraceFlushResult::Ok;
		});
		runner.check(trace.flushAndWait(1000), "flush DropOldestPending");
		runner.check(flushed.size() == 2 && flushed[0] == 2 && flushed[1] == 3, "DropOldestPending keeps newest two");
		runner.check(trace.getDiagnostics().droppedLogCount == 1, "DropOldestPending dropped count");
		runner.check(trace.end(), "end DropOldestPending");
	}
	resetClock();
	{
		Trace trace;
		std::vector<uint64_t> flushed;
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 2;
		config.overflowPolicy = TraceOverflowPolicy::DropNewest;
		runner.check(trace.init(config), "init DropNewest");
		trace.info("P", "one");
		trace.info("P", "two");
		trace.info("P", "three");
		trace.onFlush([&flushed](const TraceLogBatch &batch) {
			for (const TraceLog &log : batch.logs) {
				flushed.push_back(log.sequence);
			}
			return TraceFlushResult::Ok;
		});
		runner.check(trace.flushAndWait(1000), "flush DropNewest");
		runner.check(flushed.size() == 2 && flushed[0] == 1 && flushed[1] == 2, "DropNewest keeps oldest two");
		runner.check(trace.getDiagnostics().droppedLogCount == 1, "DropNewest dropped count");
		runner.check(trace.end(), "end DropNewest");
	}
	resetClock();
	{
		Trace trace;
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 0;
		runner.check(trace.init(config), "init maxPendingLogs zero");
		trace.info("P", "one");
		runner.check(trace.getDiagnostics().pendingLogCount == 0, "maxPendingLogs zero disables pending queue");
		runner.check(trace.getDiagnostics().droppedLogCount == 1, "maxPendingLogs zero counts persistence drop");
		runner.check(trace.end(), "end maxPendingLogs zero");
	}
}

void blockingAndFlushImmediately(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 1;
		config.overflowPolicy = TraceOverflowPolicy::BlockCaller;
		config.retryIntervalMs = 100;
		config.blockCallerTimeoutMs = 20;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Retry; });
		runner.check(trace.init(config), "init BlockCaller");
		runner.check(trace.info("B", "one"), "first BlockCaller log");
		TraceResult result = trace.info("B", "two");
		runner.check(!result && result.status == TraceStatus::Timeout, "BlockCaller times out when flush cannot free space");
		trace.end(20);
	}
	resetClock();
	{
		Trace trace;
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 1;
		config.overflowPolicy = TraceOverflowPolicy::FlushImmediately;
		config.blockCallerTimeoutMs = 1000;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Ok; });
		runner.check(trace.init(config), "init FlushImmediately");
		runner.check(trace.info("F", "one"), "first FlushImmediately log");
		runner.check(trace.info("F", "two"), "FlushImmediately frees space and queues");
		runner.check(trace.getDiagnostics().flushSuccessCount > 0, "FlushImmediately triggered flush");
		runner.check(trace.end(), "end FlushImmediately");
	}
}

void flushResults(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		int attempts = 0;
		trace.onFlush([&attempts](const TraceLogBatch &) {
			attempts++;
			return attempts == 1 ? TraceFlushResult::Retry : TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.retryIntervalMs = 25;
		runner.check(trace.init(config), "init retry flush");
		trace.info("R", "one");
		runner.check(trace.flushAndWait(5000), "retry then ok succeeds");
		TraceDiag diag = trace.getDiagnostics();
		runner.check(diag.flushRetryCount == 1, "retry diagnostic increments");
		runner.check(diag.flushSuccessCount == 1, "success diagnostic increments after retry");
		runner.check(diag.pendingLogCount == 0, "retry then ok clears pending");
		runner.check(trace.end(), "end retry flush");
	}
	resetClock();
	{
		Trace trace;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Failed; });
		runner.check(trace.init(baseConfig()), "init failed flush");
		trace.info("X", "one");
		TraceResult result = trace.flushAndWait(1000);
		runner.check(!result && result.status == TraceStatus::FlushFailed, "failed flush returns FlushFailed");
		runner.check(trace.getDiagnostics().pendingLogCount == 1, "failed flush retains pending");
		trace.end(50);
	}
	resetClock();
	{
		Trace trace;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Retry; });
		TraceConfig config = baseConfig();
		config.retryIntervalMs = 100;
		runner.check(trace.init(config), "init retry timeout");
		trace.info("T", "one");
		TraceResult result = trace.flushAndWait(20);
		runner.check(!result && result.status == TraceStatus::Timeout, "retry flushAndWait times out");
		runner.check(trace.getDiagnostics().pendingLogCount == 1, "retry timeout retains pending");
		trace.end(20);
	}
}

void callbacksRunOutsideLock(TestRunner &runner) {
	resetClock();
	Trace trace;
	bool logCallbackOk = false;
	bool flushCallbackOk = false;
	trace.onLog([&trace, &logCallbackOk](const TraceLog &) {
		TraceDiag diag = trace.getDiagnostics();
		logCallbackOk = diag.recentLogCount > 0;
	});
	trace.onFlush([&trace, &flushCallbackOk](const TraceLogBatch &) {
		TraceDiag diag = trace.getDiagnostics();
		flushCallbackOk = diag.pendingLogCount > 0;
		return TraceFlushResult::Ok;
	});
	runner.check(trace.init(baseConfig()), "init callbacksRunOutsideLock");
	trace.info("CB", "one");
	runner.check(trace.flushAndWait(1000), "flush callbacksRunOutsideLock");
	runner.check(waitUntil([&logCallbackOk]() { return logCallbackOk; }), "onLog can call getDiagnostics");
	runner.check(flushCallbackOk, "onFlush can call getDiagnostics");
	runner.check(trace.end(), "end callbacksRunOutsideLock");
}

void truncation(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.maxTagLength = 3;
	config.maxMessageLength = 5;
	config.maxFormattedLength = 4;
	runner.check(trace.init(config), "init truncation");
	trace.info("ABCDEFG", "123456789");
	TraceLog direct = trace.getLastLog();
	runner.check(direct.tag == "ABC", "tag truncated");
	runner.check(direct.message == "12345", "direct message truncated");
	runner.check(direct.truncated, "direct log marked truncated");
	runner.check(trace.getDiagnostics().truncatedLogCount == 1, "truncated count once for tag and message");
	trace.infof("FMT", "abcdef%d", 7);
	TraceLog formatted = trace.getLastLog();
	runner.check(formatted.message == "abcd", "printf message uses formatted cap");
	runner.check(trace.getDiagnostics().truncatedLogCount == 2, "printf truncation counted");
	JsonDocument doc;
	doc.setSerialized("123456789");
	trace.info("JSON", doc);
	TraceLog json = trace.getLastLog();
	runner.check(json.message == "1234", "json message uses formatted cap");
	runner.check(trace.getDiagnostics().truncatedLogCount == 3, "json truncation counted");
	runner.check(trace.end(), "end truncation");
}

void shutdownResults(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Ok; });
		runner.check(trace.init(baseConfig()), "init end success");
		trace.info("END", "ok");
		runner.check(trace.end(1000), "end returns success after final flush");
	}
	resetClock();
	{
		Trace trace;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Failed; });
		runner.check(trace.init(baseConfig()), "init end failed");
		trace.info("END", "failed");
		TraceResult result = trace.end(1000);
		runner.check(!result && result.status == TraceStatus::FlushFailed, "end returns FlushFailed");
	}
	resetClock();
	{
		Trace trace;
		trace.onFlush([](const TraceLogBatch &) { return TraceFlushResult::Retry; });
		TraceConfig config = baseConfig();
		config.retryIntervalMs = 100;
		runner.check(trace.init(config), "init end retry timeout");
		trace.info("END", "retry");
		TraceResult result = trace.end(20);
		runner.check(!result && result.status == TraceStatus::Timeout, "end returns Timeout for retry budget exhaustion");
	}
}

} // namespace

int main() {
	TestRunner runner;
	realtimeWorksWithoutRecent(runner);
	smallRecentDoesNotDropRealtime(runner);
	realtimeOverflowDiagnostics(runner);
	realtimeDisabledHasNoRealtimeDiagnostics(runner);
	pendingOverflowPolicies(runner);
	blockingAndFlushImmediately(runner);
	flushResults(runner);
	callbacksRunOutsideLock(runner);
	truncation(runner);
	shutdownResults(runner);

	if (runner.failed != 0) {
		std::cerr << runner.failed << " host Trace tests failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "host Trace tests passed\n";
	return EXIT_SUCCESS;
}

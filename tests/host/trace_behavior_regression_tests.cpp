#include <Trace.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

std::atomic<uint64_t> trace_host_millis{0};
std::atomic<int> trace_host_active_tasks{0};

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
			std::cerr << "FAIL: " << message << '\n';
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
	config.memory.allocation = Strata::Placement::Internal;
	config.memory.taskStack = Strata::Placement::Internal;
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
	std::atomic<int> observed{0};
	trace.setStream(&stream);
	trace.onLog([&observed](const TraceLog &log) {
		if (log.message == "ready") {
			observed.fetch_add(1);
		}
	});

	TraceConfig config = baseConfig();
	config.maxRecentLogs = 0;
	runner.check(trace.init(config), "init realtimeWorksWithoutRecent");
	runner.check(trace.info("BOOT", "ready"), "log realtimeWorksWithoutRecent");
	runner.check(waitUntil([&observed]() { return observed.load() == 1; }), "onLog fires with no recent history");
	runner.check(stream.output.find("[I][BOOT] - ready") != std::string::npos, "stream writes with no recent history");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.recentLogCount == 0, "recent history disabled");
	runner.check(diag.realtimeLogCount == 1, "realtime queued count increments");
	runner.check(trace.end(), "end realtimeWorksWithoutRecent");
}

void smallRecentDoesNotDropRealtime(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::atomic<int> observed{0};
	trace.onLog([&observed](const TraceLog &) { observed.fetch_add(1); });
	TraceConfig config = baseConfig();
	config.maxRecentLogs = 1;
	config.maxRealtimeLogs = 10;
	runner.check(trace.init(config), "init smallRecentDoesNotDropRealtime");
	for (int i = 0; i < 5; ++i) {
		runner.check(trace.infof("BURST", "item=%d", i), "burst log");
	}
	runner.check(waitUntil([&observed]() { return observed.load() == 5; }), "all realtime callbacks delivered");
	runner.check(trace.getLogs().size() == 1, "recent history keeps only one");
	runner.check(trace.end(), "end smallRecentDoesNotDropRealtime");
}

void realtimeOverflowDiagnostics(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::atomic<int> observed{0};
	trace.onLog([&observed](const TraceLog &) {
		observed.fetch_add(1);
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
	runner.check(observed.load() < 100, "overflow drops some callback deliveries");
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
		std::atomic<int> attempts{0};
		trace.onFlush([&attempts](const TraceLogBatch &) {
			return attempts.fetch_add(1) == 0 ? TraceFlushResult::Retry : TraceFlushResult::Ok;
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

void retryBackoffRequests(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		std::atomic<int> attempts{0};
		std::vector<uint64_t> attemptTimes;
		trace.onFlush([&attempts, &attemptTimes](const TraceLogBatch &) {
			attempts.fetch_add(1);
			attemptTimes.push_back(trace_host_millis.load());
			return TraceFlushResult::Retry;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 1;
		config.overflowPolicy = TraceOverflowPolicy::BlockCaller;
		config.retryIntervalMs = 1000;
		config.blockCallerTimeoutMs = 20;
		runner.check(trace.init(config), "init normal retry gating");
		runner.check(trace.info("R", "one"), "first normal retry gating log");
		TraceResult flushResult = trace.flushAndWait(20);
		runner.check(!flushResult && flushResult.status == TraceStatus::Timeout, "initial retry waits for retry interval");
		TraceResult blocked = trace.info("R", "two");
		runner.check(!blocked && blocked.status == TraceStatus::Timeout, "normal full-queue request waits for space");
		bool retrySpacingOk = true;
		for (size_t i = 1; i < attemptTimes.size(); ++i) {
			if (attemptTimes[i] < attemptTimes[i - 1] + config.retryIntervalMs) {
				retrySpacingOk = false;
			}
		}
		runner.check(retrySpacingOk, "normal full-queue request does not bypass retry interval");
		trace.end(20);
	}
	resetClock();
	{
		Trace trace;
		std::atomic<int> attempts{0};
		trace.onFlush([&attempts](const TraceLogBatch &) {
			return attempts.fetch_add(1) == 0 ? TraceFlushResult::Retry : TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 2;
		config.retryIntervalMs = 1000;
		runner.check(trace.init(config), "init urgent retry bypass");
		runner.check(trace.info("R", "one"), "first urgent retry bypass log");
		TraceResult flushResult = trace.flushAndWait(20);
		runner.check(!flushResult && flushResult.status == TraceStatus::Timeout, "urgent retry bypass setup times out");
		const int attemptsBeforeUrgent = attempts.load();
		runner.check(trace.error("R", "urgent"), "urgent log accepted");
		runner.check(
		    waitUntil([&attempts, attemptsBeforeUrgent]() { return attempts.load() > attemptsBeforeUrgent; }, 100),
		    "urgent request triggers another flush attempt"
		);
		runner.check(trace.end(), "end urgent retry bypass");
	}
}

void callbacksRunOutsideLock(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::atomic<bool> logCallbackOk{false};
	std::atomic<bool> flushCallbackOk{false};
	trace.onLog([&trace, &logCallbackOk](const TraceLog &) {
		TraceDiag diag = trace.getDiagnostics();
		logCallbackOk.store(diag.recentLogCount > 0);
	});
	trace.onFlush([&trace, &flushCallbackOk](const TraceLogBatch &) {
		TraceDiag diag = trace.getDiagnostics();
		flushCallbackOk.store(diag.pendingLogCount > 0);
		return TraceFlushResult::Ok;
	});
	runner.check(trace.init(baseConfig()), "init callbacksRunOutsideLock");
	trace.info("CB", "one");
	runner.check(trace.flushAndWait(1000), "flush callbacksRunOutsideLock");
	runner.check(waitUntil([&logCallbackOk]() { return logCallbackOk.load(); }), "onLog can call getDiagnostics");
	runner.check(flushCallbackOk.load(), "onFlush can call getDiagnostics");
	runner.check(trace.end(), "end callbacksRunOutsideLock");
}

void realtimeDetachStopsFutureRecords(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::atomic<int> observed{0};
	trace.onLog([&trace, &observed](const TraceLog &) {
		observed.fetch_add(1);
		trace.onLog(nullptr);
	});
	TraceConfig config = baseConfig();
	config.maxRealtimeLogs = 4;
	runner.check(trace.init(config), "init realtimeDetachStopsFutureRecords");
	runner.check(trace.info("RT", "one"), "first realtime detach log");
	runner.check(trace.info("RT", "two"), "second realtime detach log");
	runner.check(waitUntil([&observed]() { return observed.load() == 1; }), "detached realtime callback receives first record");
	vTaskDelay(10);
	runner.check(observed.load() == 1, "detached realtime callback does not receive later records");
	runner.check(trace.end(), "end realtimeDetachStopsFutureRecords");
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

void cappedFlushBarriers(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		std::vector<size_t> batchSizes;
		trace.onFlush([&batchSizes](const TraceLogBatch &batch) {
			batchSizes.push_back(batch.size());
			return TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 4;
		config.maxFlushBatchLogs = 0;
		runner.check(trace.init(config), "init uncapped flush batch");
		trace.info("B", "one");
		trace.info("B", "two");
		trace.info("B", "three");
		runner.check(trace.flushAndWait(1000), "flush uncapped batch");
		runner.check(batchSizes.size() == 1 && batchSizes[0] == 3, "uncapped flush uses one batch");
		runner.check(trace.end(), "end uncapped flush batch");
	}

	resetClock();
	{
		Trace trace;
		std::vector<std::vector<uint64_t>> batches;
		trace.onFlush([&batches](const TraceLogBatch &batch) {
			std::vector<uint64_t> sequences;
			for (const TraceLog &log : batch.logs) {
				sequences.push_back(log.sequence);
			}
			batches.push_back(sequences);
			return TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 5;
		config.maxFlushBatchLogs = 2;
		runner.check(trace.init(config), "init capped flush split");
		for (int i = 1; i <= 5; ++i) {
			runner.check(trace.infof("B", "item=%d", i), "capped split log");
		}
		runner.check(trace.flushAndWait(1000), "flush capped split");
		runner.check(
		    batches.size() == 3 && batches[0].size() == 2 && batches[0][0] == 1 &&
		        batches[0][1] == 2 && batches[1].size() == 2 && batches[1][0] == 3 &&
		        batches[1][1] == 4 && batches[2].size() == 1 && batches[2][0] == 5,
		    "capped flush splits oldest-to-newest"
		);
		runner.check(trace.end(), "end capped flush split");
	}

	resetClock();
	{
		Trace trace;
		std::vector<std::vector<uint64_t>> batches;
		bool appendedDuringFlush = false;
		trace.onFlush([&trace, &batches, &appendedDuringFlush](const TraceLogBatch &batch) {
			std::vector<uint64_t> sequences;
			for (const TraceLog &log : batch.logs) {
				sequences.push_back(log.sequence);
			}
			batches.push_back(sequences);
			if (!appendedDuringFlush) {
				appendedDuringFlush = true;
				trace.info("B", "late");
			}
			return TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 5;
		config.maxFlushBatchLogs = 1;
		runner.check(trace.init(config), "init capped flush barrier");
		trace.info("B", "one");
		trace.info("B", "two");
		runner.check(trace.flushAndWait(1000), "flush capped barrier");
		TraceDiag diag = trace.getDiagnostics();
		runner.check(
		    batches.size() == 2 && batches[0].size() == 1 && batches[0][0] == 1 &&
		        batches[1].size() == 1 && batches[1][0] == 2,
		    "active flush does not chase appended log"
		);
		runner.check(diag.pendingLogCount == 1, "late log remains pending after barrier flush");
		runner.check(trace.flushAndWait(1000), "flush late barrier log");
		runner.check(trace.getDiagnostics().pendingLogCount == 0, "late log flushes later");
		runner.check(trace.end(), "end capped flush barrier");
	}

	resetClock();
	{
		Trace trace;
		std::vector<std::vector<uint64_t>> batches;
		int attempts = 0;
		trace.onFlush([&batches, &attempts](const TraceLogBatch &batch) {
			attempts++;
			std::vector<uint64_t> sequences;
			for (const TraceLog &log : batch.logs) {
				sequences.push_back(log.sequence);
			}
			batches.push_back(sequences);
			return attempts == 2 ? TraceFlushResult::Retry : TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 4;
		config.maxFlushBatchLogs = 2;
		config.retryIntervalMs = 25;
		runner.check(trace.init(config), "init capped retry batch");
		for (int i = 1; i <= 4; ++i) {
			runner.check(trace.infof("R", "item=%d", i), "capped retry log");
		}
		runner.check(trace.flushAndWait(1000), "flush capped retry batch");
		runner.check(
		    batches.size() == 3 && batches[0].size() == 2 && batches[0][0] == 1 &&
		        batches[0][1] == 2 && batches[1].size() == 2 && batches[1][0] == 3 &&
		        batches[1][1] == 4 && batches[2].size() == 2 && batches[2][0] == 3 &&
		        batches[2][1] == 4,
		    "retry restarts from first remaining capped batch"
		);
		runner.check(trace.getDiagnostics().pendingLogCount == 0, "retry eventually clears pending");
		runner.check(trace.end(), "end capped retry batch");
	}

	resetClock();
	{
		Trace trace;
		std::vector<std::vector<uint64_t>> batches;
		int attempts = 0;
		bool failSecondBatch = true;
		trace.onFlush([&batches, &attempts, &failSecondBatch](const TraceLogBatch &batch) {
			attempts++;
			std::vector<uint64_t> sequences;
			for (const TraceLog &log : batch.logs) {
				sequences.push_back(log.sequence);
			}
			batches.push_back(sequences);
			if (failSecondBatch && attempts == 2) {
				return TraceFlushResult::Failed;
			}
			return TraceFlushResult::Ok;
		});
		TraceConfig config = baseConfig();
		config.maxPendingLogs = 4;
		config.maxFlushBatchLogs = 2;
		runner.check(trace.init(config), "init capped failed batch");
		for (int i = 1; i <= 4; ++i) {
			runner.check(trace.infof("F", "item=%d", i), "capped failed log");
		}
		TraceResult failed = trace.flushAndWait(1000);
		runner.check(!failed && failed.status == TraceStatus::FlushFailed, "capped failed batch returns FlushFailed");
		runner.check(trace.getDiagnostics().pendingLogCount == 2, "failed batch keeps remaining logs");
		failSecondBatch = false;
		runner.check(trace.flushAndWait(1000), "later flush retries failed batch");
		runner.check(
		    batches.size() == 3 && batches[0].size() == 2 && batches[0][0] == 1 &&
		        batches[0][1] == 2 && batches[1].size() == 2 && batches[1][0] == 3 &&
		        batches[1][1] == 4 && batches[2].size() == 2 && batches[2][0] == 3 &&
		        batches[2][1] == 4,
		    "failed batch restarts from first remaining capped batch"
		);
		runner.check(trace.getDiagnostics().pendingLogCount == 0, "failed batch later clears pending");
		runner.check(trace.end(), "end capped failed batch");
	}
}

void ringOrderAndSequenceConsistency(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::vector<uint64_t> realtimeSequences;
	std::vector<uint64_t> flushedSequences;
	trace.onLog([&realtimeSequences](const TraceLog &log) { realtimeSequences.push_back(log.sequence); });
	trace.onFlush([&flushedSequences](const TraceLogBatch &batch) {
		for (const TraceLog &log : batch.logs) {
			flushedSequences.push_back(log.sequence);
		}
		return TraceFlushResult::Ok;
	});

	TraceConfig config = baseConfig();
	config.maxRecentLogs = 3;
	config.maxRealtimeLogs = 5;
	config.maxPendingLogs = 3;
	config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
	runner.check(trace.init(config), "init ringOrderAndSequenceConsistency");
	for (int i = 1; i <= 5; ++i) {
		runner.check(trace.infof("SEQ", "item=%d", i), "sequence log");
	}
	runner.check(waitUntil([&realtimeSequences]() { return realtimeSequences.size() == 5; }), "realtime sees five logs");
	TraceLogList recent = trace.getLogs();
	runner.check(
	    recent.size() == 3 && recent[0].sequence == 3 && recent[1].sequence == 4 && recent[2].sequence == 5,
	    "recent query returns wrapped records oldest-to-newest"
	);
	runner.check(
	    realtimeSequences.size() == 5 && realtimeSequences[0] == 1 && realtimeSequences[1] == 2 &&
	        realtimeSequences[2] == 3 && realtimeSequences[3] == 4 && realtimeSequences[4] == 5,
	    "realtime callback order is oldest-to-newest"
	);
	runner.check(trace.flushAndWait(1000), "flush ringOrderAndSequenceConsistency");
	runner.check(
	    flushedSequences.size() == 3 && flushedSequences[0] == 3 && flushedSequences[1] == 4 &&
	        flushedSequences[2] == 5,
	    "flush batch returns wrapped pending records oldest-to-newest"
	);
	runner.check(trace.end(), "end ringOrderAndSequenceConsistency");
}

void runtimeCapClamping(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.maxTagLength = TRACE_RECORD_MAX_TAG_LENGTH + 100;
	config.maxMessageLength = 0;
	config.maxFormattedLength = 0;
	runner.check(trace.init(config), "init runtimeCapClamping");
	std::string longTag(TRACE_RECORD_MAX_TAG_LENGTH + 10, 'T');
	std::string longMessage(TRACE_RECORD_MAX_MESSAGE_LENGTH + 10, 'M');
	runner.check(trace.info(longTag.c_str(), longMessage), "log runtimeCapClamping");
	TraceLog log = trace.getLastLog();
	runner.check(log.tag.size() == TRACE_RECORD_MAX_TAG_LENGTH, "tag cap clamps to compile maximum");
	runner.check(log.message.size() == TRACE_RECORD_MAX_MESSAGE_LENGTH, "message zero runtime cap uses compile maximum");
	runner.check(log.truncated, "runtime cap clamping marks truncation");
	runner.check(trace.end(), "end runtimeCapClamping");
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
	retryBackoffRequests(runner);
	callbacksRunOutsideLock(runner);
	realtimeDetachStopsFutureRecords(runner);
	truncation(runner);
	shutdownResults(runner);
	cappedFlushBarriers(runner);
	ringOrderAndSequenceConsistency(runner);
	runtimeCapClamping(runner);

	if (runner.failed != 0) {
		std::cerr << runner.failed << " behavior regression test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Trace behavior regression tests passed\n";
	return EXIT_SUCCESS;
}

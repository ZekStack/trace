#include <Trace.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <vector>

std::atomic<uint64_t> trace_host_millis{0};
std::atomic<size_t> trace_host_global_allocations{0};

void *operator new(std::size_t size) {
	if (size == 0) {
		size = 1;
	}
	void *ptr = std::malloc(size);
	if (ptr == nullptr) {
		throw std::bad_alloc();
	}
	trace_host_global_allocations.fetch_add(1, std::memory_order_relaxed);
	return ptr;
}

void *operator new[](std::size_t size) {
	if (size == 0) {
		size = 1;
	}
	void *ptr = std::malloc(size);
	if (ptr == nullptr) {
		throw std::bad_alloc();
	}
	trace_host_global_allocations.fetch_add(1, std::memory_order_relaxed);
	return ptr;
}

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }

namespace {
struct AllocationScope {
	size_t start = trace_host_global_allocations.load(std::memory_order_relaxed);
	size_t delta() const {
		return trace_host_global_allocations.load(std::memory_order_relaxed) - start;
	}
};

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

bool waitUntil(const std::function<bool()> &predicate, uint32_t timeoutMs = 1000) {
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

void defaultPolicy(TestRunner &runner) {
	TraceConfig config;
	runner.check(
	    config.memory.allocation == Strata::Placement::PreferExternal,
	    "default allocation prefers external memory"
	);
	runner.check(
	    config.memory.taskStack == Strata::Placement::PreferExternal,
	    "default task stack prefers external memory"
	);
	runner.check(!config.realtimeAllocation.has_value(), "realtime allocation inherits by default");
}

void basicLifecycleAndQueries(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	runner.check(trace.init(config), "basic init");
	runner.check(trace.info("BOOT", "ready"), "basic log");
	TraceLog last = trace.getLastLog();
	runner.check(last.tag == "BOOT", "last log tag");
	runner.check(last.message == "ready", "last log message");
	runner.check(last.formatted == "[I][BOOT] - ready", "formatted output");
	TraceLogList logs = trace.getLogs();
	runner.check(logs.size() == 1, "query returns one log");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.recentLogCount == 1, "diagnostic recent count");
	runner.check(
	    diag.requestedAllocationPlacement == Strata::Placement::PreferExternal,
	    "diagnostic allocation placement"
	);
	runner.check(
	    diag.requestedRealtimeAllocationPlacement == Strata::Placement::PreferExternal,
	    "diagnostic inherited realtime placement"
	);
	runner.check(
	    diag.requestedTaskStackPlacement == Strata::Placement::PreferExternal,
	    "diagnostic task placement"
	);
	runner.check(diag.recentAllocatedBytes > 0, "recent storage bytes reported");
	runner.check(diag.pendingAllocatedBytes > 0, "pending storage bytes reported");
	runner.check(trace.end(), "basic end");
}

void internalPolicy(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.memory.allocation = Strata::Placement::Internal;
	config.memory.taskStack = Strata::Placement::Internal;
	config.realtimeAllocation = Strata::Placement::Internal;
	runner.check(trace.init(config), "internal policy init");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(
	    diag.requestedAllocationPlacement == Strata::Placement::Internal,
	    "internal allocation requested"
	);
	runner.check(
	    diag.requestedRealtimeAllocationPlacement == Strata::Placement::Internal,
	    "internal realtime requested"
	);
	runner.check(
	    diag.requestedTaskStackPlacement == Strata::Placement::Internal,
	    "internal task stack requested"
	);
	runner.check(trace.end(), "internal policy end");
}

void requiredExternalFailuresAreAtomic(TestRunner &runner) {
	resetClock();
	{
		Trace trace;
		TraceConfig config = baseConfig();
		config.memory.allocation = Strata::Placement::RequireExternal;
		config.memory.taskStack = Strata::Placement::Internal;
		TraceResult result = trace.init(config);
		runner.check(!result && result.status == TraceStatus::OutOfMemory, "required external buffers fail on generic host");
		runner.check(trace.end(), "failed buffer init leaves trace clean");
	}
	resetClock();
	{
		Trace trace;
		TraceConfig config = baseConfig();
		config.memory.allocation = Strata::Placement::Internal;
		config.memory.taskStack = Strata::Placement::RequireExternal;
		TraceResult result = trace.init(config);
		runner.check(!result && result.status == TraceStatus::TaskCreateFailed, "required external stack fails on generic host");
		runner.check(trace.end(), "failed task init leaves trace clean");
	}
}

void disabledRequiredExternalQueueDoesNotAllocate(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.memory.allocation = Strata::Placement::Internal;
	config.memory.taskStack = Strata::Placement::Internal;
	config.realtimeAllocation = Strata::Placement::RequireExternal;
	config.maxRealtimeLogs = 0;
	runner.check(trace.init(config), "disabled required-external realtime queue succeeds");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.realtimeAllocatedBytes == 0, "disabled realtime queue allocates zero bytes");
	runner.check(diag.realtimeStorageRegion == Strata::Region::Unknown, "disabled realtime region unknown");
	runner.check(trace.end(), "disabled realtime end");
}

void realtimeDelivery(TestRunner &runner) {
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
	config.realtimeAllocation = Strata::Placement::Internal;
	runner.check(trace.init(config), "realtime init");
	runner.check(trace.info("BOOT", "ready"), "realtime log");
	runner.check(waitUntil([&]() { return observed.load() == 1; }), "realtime callback delivered");
	runner.check(stream.output.find("[I][BOOT] - ready") != std::string::npos, "stream output delivered");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.realtimeLogCount == 1, "realtime diagnostic count");
	runner.check(
	    diag.requestedRealtimeAllocationPlacement == Strata::Placement::Internal,
	    "realtime override visible in diagnostics"
	);
	runner.check(trace.end(), "realtime end");
}

void flushBatches(TestRunner &runner) {
	resetClock();
	Trace trace;
	std::vector<uint64_t> flushed;
	trace.onFlush([&flushed](const TraceLogBatch &batch) {
		for (const TraceLog &log : batch.logs) {
			flushed.push_back(log.sequence);
		}
		return TraceFlushResult::Ok;
	});
	TraceConfig config = baseConfig();
	config.maxFlushBatchLogs = 2;
	runner.check(trace.init(config), "flush init");
	trace.info("F", "one");
	trace.info("F", "two");
	trace.info("F", "three");
	runner.check(trace.flushAndWait(1000), "flush completes");
	runner.check(flushed.size() == 3, "all records flushed across batches");
	runner.check(flushed[0] == 1 && flushed[1] == 2 && flushed[2] == 3, "flush preserves sequence order");
	runner.check(trace.getDiagnostics().pendingLogCount == 0, "flush clears pending queue");
	runner.check(trace.end(), "flush end");
}

void overflowPolicies(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.maxPendingLogs = 2;
	config.overflowPolicy = TraceOverflowPolicy::DropOldestPending;
	runner.check(trace.init(config), "overflow init");
	trace.info("P", "one");
	trace.info("P", "two");
	trace.info("P", "three");
	TraceDiag diag = trace.getDiagnostics();
	runner.check(diag.pendingLogCount == 2, "pending queue stays bounded");
	runner.check(diag.droppedLogCount == 1, "drop-oldest increments drop count");
	runner.check(trace.end(), "overflow end");
}

void filteringAndTruncation(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.minLevel = TraceLevel::Warn;
	config.maxMessageLength = 4;
	runner.check(trace.init(config), "filter init");
	runner.check(trace.info("TAG", "hidden"), "filtered log returns success");
	runner.check(trace.warn("TAG", "123456"), "truncated warn accepted");
	TraceLogList logs = trace.getLogs();
	runner.check(logs.size() == 1, "filter omits lower level");
	runner.check(logs[0].message == "1234", "message truncated to configured bound");
	runner.check(logs[0].truncated, "truncation flag propagated");
	runner.check(trace.getDiagnostics().truncatedLogCount == 1, "truncation diagnostic increments");
	runner.check(trace.end(), "filter end");
}

void directCStringHotPathDoesNotUseGlobalNew(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.maxRealtimeLogs = 0;
	config.flushEveryLogs = 0;
	config.flushIntervalMs = 0;
	runner.check(trace.init(config), "zero-allocation init");
	AllocationScope allocations;
	for (int i = 0; i < 20; ++i) {
		runner.check(trace.info("HOT", "fixed-message"), "hot path log");
	}
	runner.check(allocations.delta() == 0, "direct C-string enqueue uses no global new after init");
	runner.check(trace.end(), "zero-allocation end");
}

void repeatedInitEnd(TestRunner &runner) {
	resetClock();
	Trace trace;
	TraceConfig config = baseConfig();
	config.memory.allocation = Strata::Placement::Internal;
	config.memory.taskStack = Strata::Placement::Internal;
	for (int i = 0; i < 3; ++i) {
		runner.check(trace.init(config), "repeated init");
		runner.check(trace.info("R", "cycle"), "repeated log");
		runner.check(trace.end(), "repeated end");
	}
}

void resultMessagesAreStatic(TestRunner &runner) {
	TraceResult ok = TraceResult::success("ok-message");
	TraceResult failure = TraceResult::failure(TraceStatus::Busy, "busy-message");
	runner.check(std::string(ok.message) == "ok-message", "success message pointer");
	runner.check(std::string(failure.message) == "busy-message", "failure message pointer");
}
} // namespace

int main() {
	TestRunner runner;
	defaultPolicy(runner);
	basicLifecycleAndQueries(runner);
	internalPolicy(runner);
	requiredExternalFailuresAreAtomic(runner);
	disabledRequiredExternalQueueDoesNotAllocate(runner);
	realtimeDelivery(runner);
	flushBatches(runner);
	overflowPolicies(runner);
	filteringAndTruncation(runner);
	directCStringHotPathDoesNotUseGlobalNew(runner);
	repeatedInitEnd(runner);
	resultMessagesAreStatic(runner);

	if (runner.failed != 0) {
		std::cerr << runner.failed << " test(s) failed\n";
		return 1;
	}
	std::cout << "Trace host tests passed\n";
	return 0;
}

#pragma once

#include "../Trace.h"
#include "TraceMutex.h"
#include "TraceStorage.h"

#include <atomic>
#include <memory>
#include <stdarg.h>

#include <strata/freertos/Task.h>

namespace trace_detail {
inline constexpr uint32_t kWaitPollMs = 10;
inline constexpr size_t kTimeBufferSize = TRACE_TIME_TEXT_BUFFER_LENGTH;
inline constexpr size_t kMinStackSizeBytes = 1024;

bool levelEnabled(TraceLevel level, TraceLevel minLevel);
bool isErrorLevel(TraceLevel level);
size_t effectiveLimit(size_t limit, size_t maximum);
} // namespace trace_detail

struct TraceImpl {
	TraceImpl() noexcept : mutex(Strata::FreeRTOS::RecursiveMutex::create()) {
	}

	TraceConfig config{};
	TraceTempoConfig tempoConfig{};
	Strata::FreeRTOS::RecursiveMutex mutex;
	TraceRingBuffer<TraceRecord> recentLogs;
	TraceRingBuffer<TraceRecord> realtimeLogs;
	TraceRingBuffer<TraceRecord> pendingLogs;
	std::shared_ptr<TraceFlushCallback> onFlush;
	std::shared_ptr<TraceLogCallback> onLog;
	Print *stream = nullptr;
	Tempo *tempo = nullptr;
	bool initialized = false;
	bool stopping = false;
	bool flushRequested = false;
	bool urgentFlushRequested = false;
	Strata::FreeRTOS::Task task;
	std::atomic<bool> taskReadyForDelete{false};
	Strata::Region taskStackRegion = Strata::Region::Unknown;
	uint64_t nextSequence = 1;
	uint32_t droppedLogCount = 0;
	uint32_t realtimeLogCount = 0;
	uint32_t droppedRealtimeLogCount = 0;
	uint32_t truncatedLogCount = 0;
	uint32_t flushSuccessCount = 0;
	uint32_t flushFailCount = 0;
	uint32_t flushRetryCount = 0;
	uint64_t lastFlushAtMs = 0;
	uint64_t lastLogAtMs = 0;
	size_t stackHighWaterMarkBytes = 0;
	uint32_t flushGeneration = 0;
	TraceFlushResult lastFlushResult = TraceFlushResult::Ok;
	uint64_t lastAcceptedPendingSequence = 0;
	uint64_t lastFlushedSequence = 0;
	uint64_t activeFlushTargetSequence = 0;
	uint64_t waitingFlushTargetSequence = 0;
	uint64_t nextFlushAttemptMs = 0;
	uint64_t shutdownDeadlineMs = 0;
	bool shutdownTimedOut = false;
	bool shutdownFlushFailed = false;

	Strata::Placement allocationPlacement() const {
		return config.memory.allocation;
	}

	Strata::Placement realtimeAllocationPlacement() const {
		return config.realtimeAllocation.value_or(config.memory.allocation);
	}

	bool initBuffers();
	void deinitBuffers();
	void wakeTask();
	void addRecentLocked(const TraceRecord &record);
	void addRealtimeLocked(const TraceRecord &record);
	uint64_t latestPendingSequenceLocked() const;
	size_t pendingFlushBatchSizeLocked(uint64_t targetSequence) const;
	uint32_t retryIntervalMsLocked() const;
	TraceResult appendLog(TraceRecord record);

	TraceLog toPublicLog(const TraceRecord &record, Strata::Placement placement);
	void formatLog(TraceLog &log);
	bool formatTempoTime(
	    const Tempo &tempoRef,
	    const TraceTempoConfig &timeConfig,
	    char *buffer,
	    size_t bufferSize
	);
	void processRealtimeLogs();

	void performFlush();
	bool shouldFlushNow();
	TickType_t waitTicks();

	bool isStopping();
	bool shouldStopForShutdown();
	TickType_t shutdownWaitTicks();
	void markTaskReadyForDelete();
	static void taskEntry(void *arg);

	static const char *levelName(TraceLevel level);
	static const char *levelColor(TraceLevel level);
};

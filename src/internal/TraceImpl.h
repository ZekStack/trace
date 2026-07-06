#pragma once

#include "../Trace.h"
#include "TraceMutex.h"
#include "TraceStorage.h"
#include "TraceTaskSupport.h"

#include <stdarg.h>
#include <string>
#include <vector>

namespace trace_detail {
inline constexpr uint32_t kWaitPollMs = 10;
inline constexpr size_t kFormatBufferSize = TRACE_FORMATTED_BUFFER_LENGTH;
inline constexpr size_t kTimeBufferSize = TRACE_TIME_TEXT_BUFFER_LENGTH;

bool levelEnabled(TraceLevel level, TraceLevel minLevel);
bool isErrorLevel(TraceLevel level);
size_t effectiveLimit(size_t limit, size_t maximum);
size_t clampedLimit(size_t length, size_t limit);
std::string copyLimited(const char *value, size_t limit, bool &truncated);
std::string truncateString(const std::string &value, size_t limit, bool &truncated);
std::string formatPrintf(const char *format, va_list args, size_t limit, bool &truncated);
std::string jsonToString(
    const JsonDocument &doc,
    TraceJsonFormat format,
    size_t limit,
    bool &truncated
);
} // namespace trace_detail

struct TraceImpl {
	TraceConfig config{};
	TraceTempoConfig tempoConfig{};
	TraceMutex mutex;
	TraceRingBuffer<TraceRecord> recentLogs;
	TraceRingBuffer<TraceRecord> realtimeLogs;
	TraceRingBuffer<TraceRecord> pendingLogs;
	TraceFlushCallback onFlush;
	TraceLogCallback onLog;
	Print *stream = nullptr;
	Tempo *tempo = nullptr;
	bool initialized = false;
	bool stopping = false;
	bool flushRequested = false;
	bool urgentFlushRequested = false;
	TaskHandle_t taskHandle = nullptr;
	bool createdWithCaps = false;
	TraceStackType actualStackType = TraceStackType::Internal;
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
	uint64_t nextFlushAttemptMs = 0;
	uint64_t shutdownDeadlineMs = 0;
	bool shutdownTimedOut = false;
	bool shutdownFlushFailed = false;

	bool initBuffers();
	void deinitBuffers();
	void wakeTask();
	void addRecentLocked(const TraceRecord &record);
	void addRealtimeLocked(const TraceRecord &record);
	uint32_t retryIntervalMsLocked() const;
	TraceResult appendLog(TraceRecord record);

	TraceLog toPublicLog(const TraceRecord &record);
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
	void markTaskStopped();
	static void taskEntry(void *arg);

	static const char *levelName(TraceLevel level);
	static const char *levelColor(TraceLevel level);
};

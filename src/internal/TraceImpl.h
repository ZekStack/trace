#pragma once

#include "../Trace.h"
#include "TraceMutex.h"
#include "TraceTaskSupport.h"

#include <stdarg.h>
#include <string>
#include <vector>

namespace trace_detail {
inline constexpr uint32_t kWaitPollMs = 10;
inline constexpr size_t kFormatBufferSize = 256;
inline constexpr size_t kTimeBufferSize = 48;

bool levelEnabled(TraceLevel level, TraceLevel minLevel);
bool isErrorLevel(TraceLevel level);
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
	std::vector<TraceLog> recentLogs;
	std::vector<TraceLog> realtimeLogs;
	std::vector<TraceLog> pendingLogs;
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

	void wakeTask();
	void addRecentLocked(const TraceLog &log);
	void addRealtimeLocked(const TraceLog &log);
	uint32_t retryIntervalMsLocked() const;
	TraceResult appendLog(TraceLog log);

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

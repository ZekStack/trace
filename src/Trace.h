#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Strata.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <stdarg.h>
#include <stdio.h>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef TRACE_RECORD_MAX_TAG_LENGTH
#define TRACE_RECORD_MAX_TAG_LENGTH 32
#endif

#ifndef TRACE_RECORD_MAX_MESSAGE_LENGTH
#define TRACE_RECORD_MAX_MESSAGE_LENGTH 256
#endif

#ifndef TRACE_FORMATTED_BUFFER_LENGTH
#define TRACE_FORMATTED_BUFFER_LENGTH 384
#endif

#ifndef TRACE_TIME_TEXT_BUFFER_LENGTH
#define TRACE_TIME_TEXT_BUFFER_LENGTH 48
#endif

class Tempo;
struct TraceImpl;

enum class TraceStatus : uint8_t {
	Ok,
	NotInitialized,
	AlreadyInitialized,
	InvalidArgument,
	OutOfMemory,
	TaskCreateFailed,
	Busy,
	Timeout,
	FlushFailed,
	InternalError,
};

enum class TraceLevel : uint8_t {
	Debug = 0,
	Info = 1,
	Warn = 2,
	Error = 3,
	Fatal = 4,
};

enum class TraceOverflowPolicy : uint8_t {
	DropOldestPending,
	DropNewest,
	BlockCaller,
	FlushImmediately,
};

enum class TraceJsonFormat : uint8_t {
	Compact,
	Pretty,
};

enum class TraceTimeFormat : uint8_t {
	None,
	Full,
	Minimal,
	Iso8601,
	UnixSeconds,
	UptimeMs,
	Custom,
};

enum class TraceFlushResult : uint8_t {
	Ok,
	Failed,
	Retry,
};

struct TraceResult {
	bool result = false;
	TraceStatus status = TraceStatus::InternalError;
	const char *message = "error";

	explicit operator bool() const {
		return result;
	}

	static TraceResult success(const char *message = "ok");
	static TraceResult failure(TraceStatus status, const char *message);
};

struct TraceConfig {
	Strata::MemoryPolicy memory{
	    .allocation = Strata::Placement::PreferExternal,
	    .taskStack = Strata::Placement::PreferExternal,
	};
	std::optional<Strata::Placement> realtimeAllocation{};
	uint32_t stackSize = 4096;
	UBaseType_t priority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	size_t maxRecentLogs = 100;
	size_t maxRealtimeLogs = 100;
	size_t maxPendingLogs = 50;
	size_t maxFlushBatchLogs = 0;
	size_t flushEveryLogs = 20;
	uint32_t flushIntervalMs = 30000;
	uint32_t retryIntervalMs = 1000;
	bool flushOnError = true;
	TraceOverflowPolicy overflowPolicy = TraceOverflowPolicy::DropOldestPending;
	TraceJsonFormat jsonFormat = TraceJsonFormat::Compact;
	TraceLevel minLevel = TraceLevel::Debug;
	bool enableColors = true;
	uint32_t blockCallerTimeoutMs = 1000;
	size_t maxTagLength = 32;
	size_t maxMessageLength = 256;
	size_t maxFormattedLength = 384;
	const char *taskName = "trace-task";
};

struct TraceLog {
	explicit TraceLog(Strata::Placement placement = Strata::Placement::PreferExternal)
	    : tag(Strata::Allocator<char>{placement}),
	      message(Strata::Allocator<char>{placement}),
	      formatted(Strata::Allocator<char>{placement}),
	      timeText(Strata::Allocator<char>{placement}) {
	}

	uint64_t sequence = 0;
	TraceLevel level = TraceLevel::Info;
	Strata::String tag;
	Strata::String message;
	Strata::String formatted;
	Strata::String timeText;
	uint64_t uptimeMs = 0;
	bool truncated = false;
};

using TraceLogList = Strata::Vector<TraceLog>;

struct TraceLogBatch {
	explicit TraceLogBatch(Strata::Placement placement = Strata::Placement::PreferExternal)
	    : logs(Strata::Allocator<TraceLog>{placement}) {
	}

	TraceLogList logs;
	uint64_t createdAtUptimeMs = 0;

	size_t size() const {
		return logs.size();
	}

	bool empty() const {
		return logs.empty();
	}
};

struct TraceDiag {
	size_t recentLogCount = 0;
	size_t realtimeLogCount = 0;
	size_t pendingLogCount = 0;
	uint32_t droppedLogCount = 0;
	uint32_t droppedRealtimeLogCount = 0;
	uint32_t truncatedLogCount = 0;
	uint32_t flushSuccessCount = 0;
	uint32_t flushFailCount = 0;
	uint32_t flushRetryCount = 0;
	uint64_t lastFlushAtMs = 0;
	uint64_t lastLogAtMs = 0;
	size_t stackHighWaterMarkBytes = 0;
	Strata::Placement requestedAllocationPlacement = Strata::Placement::PreferExternal;
	Strata::Placement requestedRealtimeAllocationPlacement = Strata::Placement::PreferExternal;
	Strata::Placement requestedTaskStackPlacement = Strata::Placement::PreferExternal;
	Strata::Region taskStackRegion = Strata::Region::Unknown;
	size_t recentAllocatedBytes = 0;
	size_t realtimeAllocatedBytes = 0;
	size_t pendingAllocatedBytes = 0;
	Strata::Region recentStorageRegion = Strata::Region::Unknown;
	Strata::Region realtimeStorageRegion = Strata::Region::Unknown;
	Strata::Region pendingStorageRegion = Strata::Region::Unknown;
};

using TraceTimeFormatter = bool (*)(const Tempo &tempo, char *buffer, size_t bufferSize);

struct TraceTempoConfig {
	TraceTimeFormat format = TraceTimeFormat::Full;
	TraceTimeFormatter formatter = nullptr;
};

using TraceFlushCallback = std::function<TraceFlushResult(const TraceLogBatch &)>;
using TraceLogCallback = std::function<void(const TraceLog &)>;

class Trace {
  public:
	Trace();
	~Trace();

	Trace(const Trace &) = delete;
	Trace &operator=(const Trace &) = delete;

	TraceResult init(const TraceConfig &config = TraceConfig());
	TraceResult end(uint32_t timeoutMs = 5000);

	void onFlush(TraceFlushCallback callback);
	void onLog(TraceLogCallback callback);
	void setStream(Print *stream);
	Print *getStream();

	TraceResult attachTempo(Tempo &tempo, const TraceTempoConfig &config = TraceTempoConfig());
	void detachTempo();

	TraceResult debug(const char *tag, const char *message);
	TraceResult info(const char *tag, const char *message);
	TraceResult warn(const char *tag, const char *message);
	TraceResult error(const char *tag, const char *message);
	TraceResult fatal(const char *tag, const char *message);

	template <typename First, typename... Rest>
	TraceResult debug(const char *tag, const char *format, First first, Rest... rest) {
		return logPrintfTemplate(TraceLevel::Debug, tag, format, first, rest...);
	}

	template <typename First, typename... Rest>
	TraceResult info(const char *tag, const char *format, First first, Rest... rest) {
		return logPrintfTemplate(TraceLevel::Info, tag, format, first, rest...);
	}

	template <typename First, typename... Rest>
	TraceResult warn(const char *tag, const char *format, First first, Rest... rest) {
		return logPrintfTemplate(TraceLevel::Warn, tag, format, first, rest...);
	}

	template <typename First, typename... Rest>
	TraceResult error(const char *tag, const char *format, First first, Rest... rest) {
		return logPrintfTemplate(TraceLevel::Error, tag, format, first, rest...);
	}

	template <typename First, typename... Rest>
	TraceResult fatal(const char *tag, const char *format, First first, Rest... rest) {
		return logPrintfTemplate(TraceLevel::Fatal, tag, format, first, rest...);
	}

	TraceResult debug(const char *tag, const std::string &message);
	TraceResult info(const char *tag, const std::string &message);
	TraceResult warn(const char *tag, const std::string &message);
	TraceResult error(const char *tag, const std::string &message);
	TraceResult fatal(const char *tag, const std::string &message);

	TraceResult debug(const char *tag, const JsonDocument &doc);
	TraceResult info(const char *tag, const JsonDocument &doc);
	TraceResult warn(const char *tag, const JsonDocument &doc);
	TraceResult error(const char *tag, const JsonDocument &doc);
	TraceResult fatal(const char *tag, const JsonDocument &doc);

	TraceResult debugf(const char *tag, const char *format, ...);
	TraceResult infof(const char *tag, const char *format, ...);
	TraceResult warnf(const char *tag, const char *format, ...);
	TraceResult errorf(const char *tag, const char *format, ...);
	TraceResult fatalf(const char *tag, const char *format, ...);

	TraceResult flush();
	TraceResult flushAndWait(uint32_t timeoutMs);

	TraceDiag getDiagnostics();
	TraceLog getLastLog();
	TraceLogList getLogs();
	TraceLogList getLogs(TraceLevel level);
	TraceLogList getLastLogs(size_t count);
	TraceLogList getLogsByTag(const char *tag);

	const char *statusToString(TraceStatus status) const;
	const char *levelToString(TraceLevel level) const;
	const char *flushResultToString(TraceFlushResult result) const;

  private:
	TraceResult log(TraceLevel level, const char *tag, const std::string &message);
	TraceResult logRaw(
	    TraceLevel level,
	    const char *tag,
	    size_t tagLen,
	    const char *message,
	    size_t messageLen,
	    bool alreadyTruncated
	);
	TraceResult logJson(TraceLevel level, const char *tag, const JsonDocument &doc);
	TraceResult logVPrintf(TraceLevel level, const char *tag, const char *format, va_list args);

	template <typename... Args>
	TraceResult logPrintfTemplate(
	    TraceLevel level,
	    const char *tag,
	    const char *format,
	    Args... args
	) {
		if (format == nullptr) {
			return TraceResult::failure(TraceStatus::InvalidArgument, "format is required");
		}
		const int needed = snprintf(nullptr, 0, format, args...);
		if (needed < 0) {
			return TraceResult::failure(TraceStatus::InvalidArgument, "format failed");
		}
		const size_t limit = getMaxFormattedLength();
		const size_t outputLength = static_cast<size_t>(needed);
		const size_t boundedLength = std::min(outputLength, limit);
		char buffer[TRACE_FORMATTED_BUFFER_LENGTH + 1] = {};
		snprintf(buffer, boundedLength + 1, format, args...);
		const size_t tagLimit = getMaxTagLength();
		const size_t tagLen = boundedStrLen(tag, tagLimit + 1);
		return logRaw(level, tag, tagLen, buffer, boundedLength, outputLength > limit);
	}

	static size_t boundedStrLen(const char *value, size_t maxLen);
	size_t getMaxTagLength() const;
	size_t getMaxMessageLength() const;
	size_t getMaxFormattedLength() const;
	TraceResult log(
	    TraceLevel level,
	    const char *tag,
	    const std::string &message,
	    bool messageTruncated
	);
	Strata::UniquePtr<TraceImpl> _impl;
};

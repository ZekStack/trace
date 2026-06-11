#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <memory>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

enum class TraceStackType : uint8_t {
	Auto,
	Internal,
	Psram,
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
	std::string message;

	explicit operator bool() const {
		return result;
	}

	static TraceResult success(const char *message = "ok");
	static TraceResult failure(TraceStatus status, const char *message);
};

struct TraceConfig {
	uint32_t stackSize = 4096;
	UBaseType_t priority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	TraceStackType stackType = TraceStackType::Auto;
	size_t maxRecentLogs = 100;
	size_t maxPendingLogs = 50;
	size_t flushEveryLogs = 20;
	uint32_t flushIntervalMs = 30000;
	bool flushOnError = true;
	TraceOverflowPolicy overflowPolicy = TraceOverflowPolicy::DropOldestPending;
	TraceJsonFormat jsonFormat = TraceJsonFormat::Compact;
	TraceLevel minLevel = TraceLevel::Debug;
	uint32_t blockCallerTimeoutMs = 1000;
	const char *taskName = "trace-task";
};

struct TraceLog {
	uint64_t sequence = 0;
	TraceLevel level = TraceLevel::Info;
	std::string tag;
	std::string message;
	std::string formatted;
	std::string timeText;
	uint64_t uptimeMs = 0;
};

struct TraceLogBatch {
	std::vector<TraceLog> logs;
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
	size_t pendingLogCount = 0;
	uint32_t droppedLogCount = 0;
	uint32_t flushSuccessCount = 0;
	uint32_t flushFailCount = 0;
	uint64_t lastFlushAtMs = 0;
	uint64_t lastLogAtMs = 0;
	size_t stackHighWaterMarkBytes = 0;
	TraceStackType requestedStackType = TraceStackType::Auto;
	TraceStackType actualStackType = TraceStackType::Internal;
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
	std::vector<TraceLog> getLogs();
	std::vector<TraceLog> getLogs(TraceLevel level);
	std::vector<TraceLog> getLastLogs(size_t count);
	std::vector<TraceLog> getLogsByTag(const char *tag);

	const char *statusToString(TraceStatus status) const;
	const char *levelToString(TraceLevel level) const;
	const char *flushResultToString(TraceFlushResult result) const;

  private:
	TraceResult log(TraceLevel level, const char *tag, const std::string &message);
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
		std::vector<char> buffer(static_cast<size_t>(needed) + 1);
		snprintf(buffer.data(), buffer.size(), format, args...);
		return log(level, tag, std::string(buffer.data()));
	}

	std::unique_ptr<TraceImpl> _impl;
};

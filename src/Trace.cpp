#include "Trace.h"

#include "internal/TraceMutex.h"
#include "internal/TraceTaskSupport.h"

#include <algorithm>
#include <memory>
#include <new>

#if __has_include(<Tempo.h>)
#include <Tempo.h>
#define TRACE_HAS_TEMPO 1
#else
#define TRACE_HAS_TEMPO 0
#endif

namespace {
constexpr uint32_t kWaitPollMs = 10;
constexpr size_t kFormatBufferSize = 256;
constexpr size_t kTimeBufferSize = 48;

bool levelEnabled(TraceLevel level, TraceLevel minLevel) {
	return static_cast<uint8_t>(level) >= static_cast<uint8_t>(minLevel);
}

bool isErrorLevel(TraceLevel level) {
	return level == TraceLevel::Error || level == TraceLevel::Fatal;
}

std::string formatPrintf(const char *format, va_list args) {
	if (format == nullptr) {
		return std::string();
	}

	char stackBuffer[kFormatBufferSize];
	va_list copy;
	va_copy(copy, args);
	const int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, copy);
	va_end(copy);

	if (needed < 0) {
		return std::string();
	}
	if (static_cast<size_t>(needed) < sizeof(stackBuffer)) {
		return std::string(stackBuffer);
	}

	std::vector<char> buffer(static_cast<size_t>(needed) + 1);
	vsnprintf(buffer.data(), buffer.size(), format, args);
	return std::string(buffer.data());
}

std::string jsonToString(const JsonDocument &doc, TraceJsonFormat format) {
	String output;
	if (format == TraceJsonFormat::Pretty) {
		serializeJsonPretty(doc, output);
	} else {
		serializeJson(doc, output);
	}
	return std::string(output.c_str());
}
} // namespace

struct TraceImpl {
	TraceConfig config{};
	TraceTempoConfig tempoConfig{};
	TraceMutex mutex;
	std::vector<TraceLog> recentLogs;
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
	uint64_t lastOnLogSequence = 0;
	uint32_t droppedLogCount = 0;
	uint32_t flushSuccessCount = 0;
	uint32_t flushFailCount = 0;
	uint64_t lastFlushAtMs = 0;
	uint64_t lastLogAtMs = 0;
	size_t stackHighWaterMarkBytes = 0;
	uint32_t flushGeneration = 0;
	TraceFlushResult lastFlushResult = TraceFlushResult::Ok;

	void wakeTask() {
		TaskHandle_t handle = nullptr;
		{
			TraceLock lock(mutex);
			if (lock) {
				handle = taskHandle;
			}
		}
		if (handle != nullptr) {
			xTaskNotifyGive(handle);
		}
	}

	void addRecentLocked(const TraceLog &log) {
		if (config.maxRecentLogs == 0) {
			return;
		}
		while (recentLogs.size() >= config.maxRecentLogs) {
			recentLogs.erase(recentLogs.begin());
		}
		recentLogs.push_back(log);
	}

	TraceResult appendLog(TraceLog log) {
		bool recentAdded = false;
		const uint64_t startedMs = millis();

		while (true) {
			bool shouldNotify = false;
			TraceResult result = TraceResult::success("log queued");
			{
				TraceLock lock(mutex);
				if (!lock) {
					return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
				}
				if (!initialized || stopping) {
					return TraceResult::failure(
					    TraceStatus::NotInitialized,
					    "trace is not initialized"
					);
				}

				if (!recentAdded) {
					log.sequence = nextSequence++;
					addRecentLocked(log);
					lastLogAtMs = log.uptimeMs;
					recentAdded = true;
				}

				if (config.maxPendingLogs == 0) {
					droppedLogCount++;
					shouldNotify = true;
				} else if (pendingLogs.size() < config.maxPendingLogs) {
					pendingLogs.push_back(log);
					shouldNotify = true;
				} else if (config.overflowPolicy == TraceOverflowPolicy::DropOldestPending) {
					pendingLogs.erase(pendingLogs.begin());
					pendingLogs.push_back(log);
					droppedLogCount++;
					shouldNotify = true;
				} else if (config.overflowPolicy == TraceOverflowPolicy::DropNewest) {
					droppedLogCount++;
					shouldNotify = true;
				} else if (
				    config.overflowPolicy == TraceOverflowPolicy::BlockCaller ||
				    config.overflowPolicy == TraceOverflowPolicy::FlushImmediately
				) {
					flushRequested = true;
					shouldNotify = true;
					result = TraceResult::failure(TraceStatus::Busy, "pending queue is full");
				}

				if (config.flushEveryLogs > 0 && pendingLogs.size() >= config.flushEveryLogs) {
					flushRequested = true;
				}
				if (config.flushOnError && isErrorLevel(log.level)) {
					urgentFlushRequested = true;
					flushRequested = true;
				}
			}

			if (shouldNotify) {
				wakeTask();
			}
			if (result.status != TraceStatus::Busy) {
				return result;
			}

			const uint32_t elapsedMs = millis() - startedMs;
			if (elapsedMs >= config.blockCallerTimeoutMs) {
				TraceLock lock(mutex);
				if (lock) {
					droppedLogCount++;
				}
				return TraceResult::failure(TraceStatus::Timeout, "pending queue is full");
			}

			vTaskDelay(pdMS_TO_TICKS(kWaitPollMs));
		}
	}

	void formatLog(TraceLog &log) {
		char timeBuffer[kTimeBufferSize] = {};
		bool hasTime = false;

		Tempo *tempoSnapshot = nullptr;
		TraceTempoConfig tempoConfigSnapshot;
		{
			TraceLock lock(mutex);
			if (lock) {
				tempoSnapshot = tempo;
				tempoConfigSnapshot = tempoConfig;
			}
		}

		if (tempoSnapshot != nullptr) {
			hasTime = formatTempoTime(*tempoSnapshot, tempoConfigSnapshot, timeBuffer, sizeof(timeBuffer));
		}
		if (hasTime) {
			log.timeText = timeBuffer;
			log.formatted = std::string("[") + levelName(log.level) + "][" + log.tag + "](" +
			                log.timeText + ") - " + log.message;
		} else {
			log.timeText.clear();
			log.formatted = std::string("[") + levelName(log.level) + "][" + log.tag + "] - " +
			                log.message;
		}
	}

	bool formatTempoTime(
	    const Tempo &tempoRef,
	    const TraceTempoConfig &timeConfig,
	    char *buffer,
	    size_t bufferSize
	) {
		if (buffer == nullptr || bufferSize == 0 || timeConfig.format == TraceTimeFormat::None) {
			return false;
		}

		buffer[0] = '\0';
		if (timeConfig.format == TraceTimeFormat::Custom) {
			return timeConfig.formatter != nullptr &&
			       timeConfig.formatter(tempoRef, buffer, bufferSize) && buffer[0] != '\0';
		}

#if TRACE_HAS_TEMPO
		if (timeConfig.format == TraceTimeFormat::UnixSeconds) {
			snprintf(
			    buffer,
			    bufferSize,
			    "%llu",
			    static_cast<unsigned long long>(tempoRef.unixSeconds())
			);
			return true;
		}
		if (timeConfig.format == TraceTimeFormat::UptimeMs) {
			snprintf(buffer, bufferSize, "%llu", static_cast<unsigned long long>(millis()));
			return true;
		}

		LocalDateTime now = tempoRef.nowLocal();
		if (!now.ok) {
			return false;
		}
		if (timeConfig.format == TraceTimeFormat::Minimal) {
			snprintf(buffer, bufferSize, "%02d:%02d", now.hour, now.minute);
			return true;
		}
		if (timeConfig.format == TraceTimeFormat::Iso8601) {
			return tempoRef.formatLocal(now.utc, TempoFormat::Iso8601, buffer, bufferSize);
		}

		snprintf(
		    buffer,
		    bufferSize,
		    "%04d-%02d-%02d %02d:%02d:%02d",
		    now.year,
		    now.month,
		    now.day,
		    now.hour,
		    now.minute,
		    now.second
		);
		return true;
#else
		(void)tempoRef;
		return false;
#endif
	}

	void processRealtimeLogs() {
		TraceLogCallback callback;
		Print *streamSnapshot = nullptr;
		bool colorsEnabled = true;
		std::vector<TraceLog> logs;
		{
			TraceLock lock(mutex);
			if (!lock) {
				return;
			}
			callback = onLog;
			streamSnapshot = stream;
			colorsEnabled = config.enableColors;
			if (!callback && streamSnapshot == nullptr) {
				lastOnLogSequence = nextSequence > 0 ? nextSequence - 1 : 0;
				return;
			}
			for (const TraceLog &log : recentLogs) {
				if (log.sequence > lastOnLogSequence) {
					logs.push_back(log);
				}
			}
			if (!logs.empty()) {
				lastOnLogSequence = logs.back().sequence;
			}
		}

		for (TraceLog &log : logs) {
			formatLog(log);
			if (streamSnapshot != nullptr) {
				const char *color = colorsEnabled ? levelColor(log.level) : "";
				if (color[0] != '\0') {
					streamSnapshot->print(color);
					streamSnapshot->print(log.formatted.c_str());
					streamSnapshot->println("\033[0m");
				} else {
					streamSnapshot->println(log.formatted.c_str());
				}
			}
			if (callback) {
				callback(log);
			}
		}
	}

	void performFlush() {
		TraceFlushCallback callback;
		TraceLogBatch batch;
		uint64_t maxSequence = 0;
		{
			TraceLock lock(mutex);
			if (!lock) {
				return;
			}
			callback = onFlush;
			batch.logs = pendingLogs;
			batch.createdAtUptimeMs = millis();
			if (!batch.logs.empty()) {
				maxSequence = batch.logs.back().sequence;
			}
			flushRequested = false;
			urgentFlushRequested = false;
		}

		for (TraceLog &log : batch.logs) {
			formatLog(log);
		}

		TraceFlushResult flushResult = TraceFlushResult::Ok;
		if (callback && !batch.logs.empty()) {
			flushResult = callback(batch);
		}

		{
			TraceLock lock(mutex);
			if (!lock) {
				return;
			}
			lastFlushAtMs = millis();
			lastFlushResult = flushResult;
			flushGeneration++;
			if (flushResult == TraceFlushResult::Ok) {
				if (maxSequence > 0) {
					pendingLogs.erase(
					    std::remove_if(
					        pendingLogs.begin(),
					        pendingLogs.end(),
					        [maxSequence](const TraceLog &log) {
						        return log.sequence <= maxSequence;
					        }
					    ),
					    pendingLogs.end()
					);
				}
				flushSuccessCount++;
			} else {
				flushFailCount++;
			}
		}
	}

	bool shouldFlushNow() {
		TraceLock lock(mutex);
		if (!lock) {
			return false;
		}
		if (flushRequested || urgentFlushRequested) {
			return true;
		}
		if (pendingLogs.empty()) {
			return false;
		}
		if (config.flushEveryLogs > 0 && pendingLogs.size() >= config.flushEveryLogs) {
			return true;
		}
		if (config.flushIntervalMs == 0) {
			return false;
		}
		const uint64_t nowMs = millis();
		return lastFlushAtMs == 0 || nowMs - lastFlushAtMs >= config.flushIntervalMs;
	}

	TickType_t waitTicks() {
		TraceLock lock(mutex);
		if (!lock || config.flushIntervalMs == 0) {
			return portMAX_DELAY;
		}
		return pdMS_TO_TICKS(config.flushIntervalMs);
	}

	bool isStopping() {
		TraceLock lock(mutex);
		return lock && stopping;
	}

	void markTaskStopped() {
		TraceLock lock(mutex);
		if (lock) {
			stackHighWaterMarkBytes = trace_task_support::currentStackHighWaterMarkBytes();
			taskHandle = nullptr;
		}
	}

	static void taskEntry(void *arg) {
		TraceImpl *impl = static_cast<TraceImpl *>(arg);
		if (impl == nullptr) {
			vTaskDelete(nullptr);
			return;
		}

		while (true) {
			impl->processRealtimeLogs();

			if (impl->isStopping()) {
				if (impl->shouldFlushNow()) {
					impl->performFlush();
				}
				break;
			}

			if (impl->shouldFlushNow()) {
				impl->performFlush();
				continue;
			}

			ulTaskNotifyTake(pdTRUE, impl->waitTicks());
		}

		const bool withCaps = impl->createdWithCaps;
		impl->markTaskStopped();
		trace_task_support::deleteCurrentTask(withCaps);
	}

	static const char *levelName(TraceLevel level) {
		switch (level) {
		case TraceLevel::Debug:
			return "D";
		case TraceLevel::Info:
			return "I";
		case TraceLevel::Warn:
			return "W";
		case TraceLevel::Error:
			return "E";
		case TraceLevel::Fatal:
			return "F";
		default:
			return "?";
		}
	}

	static const char *levelColor(TraceLevel level) {
		switch (level) {
		case TraceLevel::Debug:
			return "\033[2;37m";
		case TraceLevel::Info:
			return "\033[32m";
		case TraceLevel::Warn:
			return "\033[33m";
		case TraceLevel::Error:
			return "\033[31m";
		case TraceLevel::Fatal:
			return "\033[1;91m";
		default:
			return "";
		}
	}
};

TraceResult TraceResult::success(const char *message) {
	TraceResult result;
	result.result = true;
	result.status = TraceStatus::Ok;
	result.message = message != nullptr ? message : "ok";
	return result;
}

TraceResult TraceResult::failure(TraceStatus status, const char *message) {
	TraceResult result;
	result.result = false;
	result.status = status;
	result.message = message != nullptr ? message : "error";
	return result;
}

Trace::Trace() : _impl(std::make_unique<TraceImpl>()) {
}

Trace::~Trace() {
	end(2000);
}

TraceResult Trace::init(const TraceConfig &config) {
	if (!trace_task_support::isValidStackSize(config.stackSize)) {
		return TraceResult::failure(
		    TraceStatus::InvalidArgument,
		    "stack size must be at least 1024 bytes and aligned"
		);
	}
	if (config.taskName == nullptr || config.taskName[0] == '\0') {
		return TraceResult::failure(TraceStatus::InvalidArgument, "task name is required");
	}

	bool usePsramStack = false;
	TraceStackType actualStackType = TraceStackType::Internal;
	if (config.stackType == TraceStackType::Psram) {
		if (!trace_task_support::hasExternalStackSupport()) {
			return TraceResult::failure(
			    TraceStatus::TaskCreateFailed,
			    "PSRAM task stacks are not available"
			);
		}
		usePsramStack = true;
		actualStackType = TraceStackType::Psram;
	} else if (config.stackType == TraceStackType::Auto &&
	           trace_task_support::hasExternalStackSupport()) {
		usePsramStack = true;
		actualStackType = TraceStackType::Psram;
	}

	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (_impl->initialized) {
			return TraceResult::failure(TraceStatus::AlreadyInitialized, "trace already initialized");
		}
		_impl->config = config;
		_impl->actualStackType = actualStackType;
		_impl->stopping = false;
		_impl->flushRequested = false;
		_impl->urgentFlushRequested = false;
		_impl->recentLogs.clear();
		_impl->pendingLogs.clear();
		_impl->nextSequence = 1;
		_impl->lastOnLogSequence = 0;
		_impl->droppedLogCount = 0;
		_impl->flushSuccessCount = 0;
		_impl->flushFailCount = 0;
		_impl->lastFlushAtMs = 0;
		_impl->lastLogAtMs = 0;
		_impl->stackHighWaterMarkBytes = 0;
		_impl->flushGeneration = 0;
		_impl->lastFlushResult = TraceFlushResult::Ok;
	}

	TaskHandle_t handle = nullptr;
	bool createdWithCaps = false;
	const BaseType_t created = trace_task_support::createTask(
	    &TraceImpl::taskEntry,
	    config.taskName,
	    config.stackSize,
	    _impl.get(),
	    config.priority,
	    &handle,
	    config.coreId,
	    usePsramStack,
	    createdWithCaps
	);
	if (created != pdPASS || handle == nullptr) {
		return TraceResult::failure(TraceStatus::TaskCreateFailed, "failed to create trace task");
	}

	{
		TraceLock lock(_impl->mutex);
		if (lock) {
			_impl->taskHandle = handle;
			_impl->createdWithCaps = createdWithCaps;
			_impl->initialized = true;
		}
	}

	return TraceResult::success("trace initialized");
}

TraceResult Trace::end(uint32_t timeoutMs) {
	TaskHandle_t handle = nullptr;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (!_impl->initialized) {
			return TraceResult::success("trace is not initialized");
		}
		_impl->stopping = true;
		_impl->flushRequested = true;
		handle = _impl->taskHandle;
	}
	if (handle != nullptr) {
		xTaskNotifyGive(handle);
	}

	const uint32_t startedMs = millis();
	while (true) {
		{
			TraceLock lock(_impl->mutex);
			if (lock && _impl->taskHandle == nullptr) {
				_impl->initialized = false;
				_impl->stopping = false;
				return TraceResult::success("trace ended");
			}
		}
		if (millis() - startedMs >= timeoutMs) {
			return TraceResult::failure(TraceStatus::Timeout, "trace end timed out");
		}
		vTaskDelay(pdMS_TO_TICKS(kWaitPollMs));
	}
}

void Trace::onFlush(TraceFlushCallback callback) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onFlush = callback;
	}
}

void Trace::onLog(TraceLogCallback callback) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onLog = callback;
	}
}

void Trace::setStream(Print *stream) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->stream = stream;
	}
}

Print *Trace::getStream() {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return nullptr;
	}
	return _impl->stream;
}

TraceResult Trace::attachTempo(Tempo &tempo, const TraceTempoConfig &config) {
	if (config.format == TraceTimeFormat::Custom && config.formatter == nullptr) {
		return TraceResult::failure(
		    TraceStatus::InvalidArgument,
		    "custom tempo formatter is required"
		);
	}
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
	}
	_impl->tempo = &tempo;
	_impl->tempoConfig = config;
	return TraceResult::success("tempo attached");
}

void Trace::detachTempo() {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->tempo = nullptr;
		_impl->tempoConfig = TraceTempoConfig();
	}
}

TraceResult Trace::log(TraceLevel level, const char *tag, const std::string &message) {
	if (tag == nullptr || tag[0] == '\0') {
		return TraceResult::failure(TraceStatus::InvalidArgument, "tag is required");
	}

	TraceConfig config;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (!_impl->initialized || _impl->stopping) {
			return TraceResult::failure(TraceStatus::NotInitialized, "trace is not initialized");
		}
		config = _impl->config;
	}

	if (!levelEnabled(level, config.minLevel)) {
		return TraceResult::success("log filtered");
	}

	TraceLog log;
	log.level = level;
	log.tag = tag;
	log.message = message;
	log.uptimeMs = millis();
	return _impl->appendLog(log);
}

TraceResult Trace::logJson(TraceLevel level, const char *tag, const JsonDocument &doc) {
	TraceJsonFormat format = TraceJsonFormat::Compact;
	{
		TraceLock lock(_impl->mutex);
		if (lock) {
			format = _impl->config.jsonFormat;
		}
	}
	return log(level, tag, jsonToString(doc, format));
}

TraceResult Trace::logVPrintf(
    TraceLevel level,
    const char *tag,
    const char *format,
    va_list args
) {
	if (format == nullptr) {
		return TraceResult::failure(TraceStatus::InvalidArgument, "format is required");
	}
	return log(level, tag, formatPrintf(format, args));
}

TraceResult Trace::debug(const char *tag, const char *message) {
	return log(TraceLevel::Debug, tag, message != nullptr ? message : "");
}

TraceResult Trace::info(const char *tag, const char *message) {
	return log(TraceLevel::Info, tag, message != nullptr ? message : "");
}

TraceResult Trace::warn(const char *tag, const char *message) {
	return log(TraceLevel::Warn, tag, message != nullptr ? message : "");
}

TraceResult Trace::error(const char *tag, const char *message) {
	return log(TraceLevel::Error, tag, message != nullptr ? message : "");
}

TraceResult Trace::fatal(const char *tag, const char *message) {
	return log(TraceLevel::Fatal, tag, message != nullptr ? message : "");
}

TraceResult Trace::debug(const char *tag, const std::string &message) {
	return log(TraceLevel::Debug, tag, message);
}

TraceResult Trace::info(const char *tag, const std::string &message) {
	return log(TraceLevel::Info, tag, message);
}

TraceResult Trace::warn(const char *tag, const std::string &message) {
	return log(TraceLevel::Warn, tag, message);
}

TraceResult Trace::error(const char *tag, const std::string &message) {
	return log(TraceLevel::Error, tag, message);
}

TraceResult Trace::fatal(const char *tag, const std::string &message) {
	return log(TraceLevel::Fatal, tag, message);
}

TraceResult Trace::debug(const char *tag, const JsonDocument &doc) {
	return logJson(TraceLevel::Debug, tag, doc);
}

TraceResult Trace::info(const char *tag, const JsonDocument &doc) {
	return logJson(TraceLevel::Info, tag, doc);
}

TraceResult Trace::warn(const char *tag, const JsonDocument &doc) {
	return logJson(TraceLevel::Warn, tag, doc);
}

TraceResult Trace::error(const char *tag, const JsonDocument &doc) {
	return logJson(TraceLevel::Error, tag, doc);
}

TraceResult Trace::fatal(const char *tag, const JsonDocument &doc) {
	return logJson(TraceLevel::Fatal, tag, doc);
}

TraceResult Trace::debugf(const char *tag, const char *format, ...) {
	va_list args;
	va_start(args, format);
	TraceResult result = logVPrintf(TraceLevel::Debug, tag, format, args);
	va_end(args);
	return result;
}

TraceResult Trace::infof(const char *tag, const char *format, ...) {
	va_list args;
	va_start(args, format);
	TraceResult result = logVPrintf(TraceLevel::Info, tag, format, args);
	va_end(args);
	return result;
}

TraceResult Trace::warnf(const char *tag, const char *format, ...) {
	va_list args;
	va_start(args, format);
	TraceResult result = logVPrintf(TraceLevel::Warn, tag, format, args);
	va_end(args);
	return result;
}

TraceResult Trace::errorf(const char *tag, const char *format, ...) {
	va_list args;
	va_start(args, format);
	TraceResult result = logVPrintf(TraceLevel::Error, tag, format, args);
	va_end(args);
	return result;
}

TraceResult Trace::fatalf(const char *tag, const char *format, ...) {
	va_list args;
	va_start(args, format);
	TraceResult result = logVPrintf(TraceLevel::Fatal, tag, format, args);
	va_end(args);
	return result;
}

TraceResult Trace::flush() {
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (!_impl->initialized || _impl->stopping) {
			return TraceResult::failure(TraceStatus::NotInitialized, "trace is not initialized");
		}
		_impl->flushRequested = true;
	}
	_impl->wakeTask();
	return TraceResult::success("flush requested");
}

TraceResult Trace::flushAndWait(uint32_t timeoutMs) {
	uint32_t targetGeneration = 0;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (!_impl->initialized || _impl->stopping) {
			return TraceResult::failure(TraceStatus::NotInitialized, "trace is not initialized");
		}
		targetGeneration = _impl->flushGeneration + 1;
		_impl->flushRequested = true;
	}
	_impl->wakeTask();

	const uint32_t startedMs = millis();
	while (true) {
		TraceFlushResult result = TraceFlushResult::Ok;
		bool done = false;
		{
			TraceLock lock(_impl->mutex);
			if (lock) {
				done = _impl->flushGeneration >= targetGeneration;
				result = _impl->lastFlushResult;
			}
		}
		if (done) {
			if (result == TraceFlushResult::Ok) {
				return TraceResult::success("flush completed");
			}
			return TraceResult::failure(TraceStatus::FlushFailed, "flush failed");
		}
		if (millis() - startedMs >= timeoutMs) {
			return TraceResult::failure(TraceStatus::Timeout, "flush timed out");
		}
		vTaskDelay(pdMS_TO_TICKS(kWaitPollMs));
	}
}

TraceDiag Trace::getDiagnostics() {
	TraceDiag diag;
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return diag;
	}
	diag.recentLogCount = _impl->recentLogs.size();
	diag.pendingLogCount = _impl->pendingLogs.size();
	diag.droppedLogCount = _impl->droppedLogCount;
	diag.flushSuccessCount = _impl->flushSuccessCount;
	diag.flushFailCount = _impl->flushFailCount;
	diag.lastFlushAtMs = _impl->lastFlushAtMs;
	diag.lastLogAtMs = _impl->lastLogAtMs;
	diag.stackHighWaterMarkBytes = _impl->stackHighWaterMarkBytes;
	diag.requestedStackType = _impl->config.stackType;
	diag.actualStackType = _impl->actualStackType;
	return diag;
}

TraceLog Trace::getLastLog() {
	TraceLog log;
	{
		TraceLock lock(_impl->mutex);
		if (!lock || _impl->recentLogs.empty()) {
			return log;
		}
		log = _impl->recentLogs.back();
	}
	_impl->formatLog(log);
	return log;
}

std::vector<TraceLog> Trace::getLogs() {
	std::vector<TraceLog> logs;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		logs = _impl->recentLogs;
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

std::vector<TraceLog> Trace::getLogs(TraceLevel level) {
	std::vector<TraceLog> logs;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		for (const TraceLog &log : _impl->recentLogs) {
			if (log.level == level) {
				logs.push_back(log);
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

std::vector<TraceLog> Trace::getLastLogs(size_t count) {
	std::vector<TraceLog> logs;
	{
		TraceLock lock(_impl->mutex);
		if (!lock || count == 0) {
			return logs;
		}
		const size_t start = count >= _impl->recentLogs.size() ? 0 : _impl->recentLogs.size() - count;
		for (size_t i = start; i < _impl->recentLogs.size(); ++i) {
			logs.push_back(_impl->recentLogs[i]);
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

std::vector<TraceLog> Trace::getLogsByTag(const char *tag) {
	std::vector<TraceLog> logs;
	if (tag == nullptr) {
		return logs;
	}
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		for (const TraceLog &log : _impl->recentLogs) {
			if (log.tag == tag) {
				logs.push_back(log);
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

const char *Trace::statusToString(TraceStatus status) const {
	switch (status) {
	case TraceStatus::Ok:
		return "Ok";
	case TraceStatus::NotInitialized:
		return "NotInitialized";
	case TraceStatus::AlreadyInitialized:
		return "AlreadyInitialized";
	case TraceStatus::InvalidArgument:
		return "InvalidArgument";
	case TraceStatus::OutOfMemory:
		return "OutOfMemory";
	case TraceStatus::TaskCreateFailed:
		return "TaskCreateFailed";
	case TraceStatus::Busy:
		return "Busy";
	case TraceStatus::Timeout:
		return "Timeout";
	case TraceStatus::FlushFailed:
		return "FlushFailed";
	case TraceStatus::InternalError:
		return "InternalError";
	default:
		return "Unknown";
	}
}

const char *Trace::levelToString(TraceLevel level) const {
	return TraceImpl::levelName(level);
}

const char *Trace::flushResultToString(TraceFlushResult result) const {
	switch (result) {
	case TraceFlushResult::Ok:
		return "Ok";
	case TraceFlushResult::Failed:
		return "Failed";
	case TraceFlushResult::Retry:
		return "Retry";
	default:
		return "Unknown";
	}
}

#include "internal/TraceImpl.h"

#include <cstring>

namespace {
void copyToRecordField(
    const char *source,
    size_t sourceLength,
    size_t configuredLimit,
    char *target,
    size_t targetCapacity,
    bool &truncated
) {
	if (target == nullptr || targetCapacity == 0) {
		return;
	}
	target[0] = '\0';
	if (source == nullptr) {
		return;
	}
	const size_t effective = trace_detail::effectiveLimit(configuredLimit, targetCapacity - 1);
	const size_t copied = std::min(sourceLength, effective);
	if (copied > 0) {
		memcpy(target, source, copied);
	}
	target[copied] = '\0';
	if (sourceLength > effective) {
		truncated = true;
	}
}

} // namespace

TraceResult TraceImpl::appendLog(TraceRecord record) {
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
				record.sequence = nextSequence++;
				addRecentLocked(record);
				addRealtimeLocked(record);
				lastLogAtMs = record.uptimeMs;
				if (record.truncated) {
					truncatedLogCount++;
				}
				recentAdded = true;
			}

			if (config.maxPendingLogs == 0) {
				droppedLogCount++;
				shouldNotify = true;
			} else if (pendingLogs.pushDropNewest(record)) {
				shouldNotify = true;
			} else if (config.overflowPolicy == TraceOverflowPolicy::DropOldestPending) {
				pendingLogs.pushDropOldest(record);
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
			if (config.flushOnError && trace_detail::isErrorLevel(record.level)) {
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

		vTaskDelay(pdMS_TO_TICKS(trace_detail::kWaitPollMs));
	}
}

void TraceImpl::addRecentLocked(const TraceRecord &record) {
	if (config.maxRecentLogs == 0) {
		return;
	}
	recentLogs.pushDropOldest(record);
}

void TraceImpl::addRealtimeLocked(const TraceRecord &record) {
	if (config.maxRealtimeLogs == 0 || (!onLog && stream == nullptr)) {
		return;
	}
	if (realtimeLogs.full()) {
		droppedRealtimeLogCount++;
	}
	realtimeLogs.pushDropOldest(record);
	realtimeLogCount++;
}

void TraceImpl::processRealtimeLogs() {
	while (true) {
		TraceLogCallback callback;
		Print *streamSnapshot = nullptr;
		bool colorsEnabled = true;
		TraceRecord record;
		{
			TraceLock lock(mutex);
			if (!lock || stopping || realtimeLogs.empty()) {
				return;
			}
			callback = onLog;
			streamSnapshot = stream;
			colorsEnabled = config.enableColors;
			if (!callback && streamSnapshot == nullptr) {
				realtimeLogs.clear();
				return;
			}
			if (!realtimeLogs.pop(record)) {
				return;
			}
		}

		TraceLog log = toPublicLog(record);
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

size_t Trace::boundedStrLen(const char *value, size_t maxLen) {
	if (value == nullptr) {
		return 0;
	}
	size_t length = 0;
	while (length < maxLen && value[length] != '\0') {
		length++;
	}
	return length;
}

size_t Trace::getMaxTagLength() const {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TRACE_RECORD_MAX_TAG_LENGTH;
	}
	return trace_detail::effectiveLimit(_impl->config.maxTagLength, TRACE_RECORD_MAX_TAG_LENGTH);
}

size_t Trace::getMaxMessageLength() const {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TRACE_RECORD_MAX_MESSAGE_LENGTH;
	}
	return trace_detail::effectiveLimit(
	    _impl->config.maxMessageLength,
	    TRACE_RECORD_MAX_MESSAGE_LENGTH
	);
}

size_t Trace::getMaxFormattedLength() const {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TRACE_FORMATTED_BUFFER_LENGTH;
	}
	return trace_detail::effectiveLimit(
	    _impl->config.maxFormattedLength,
	    TRACE_FORMATTED_BUFFER_LENGTH
	);
}

TraceResult Trace::log(TraceLevel level, const char *tag, const std::string &message) {
	return log(level, tag, message, false);
}

TraceResult Trace::log(
    TraceLevel level,
    const char *tag,
    const std::string &message,
    bool messageTruncated
) {
	const size_t tagLimit = getMaxTagLength();
	const size_t tagLen = boundedStrLen(tag, tagLimit + 1);
	return logRaw(level, tag, tagLen, message.data(), message.size(), messageTruncated);
}

TraceResult Trace::logRaw(
    TraceLevel level,
    const char *tag,
    size_t tagLen,
    const char *message,
    size_t messageLen,
    bool alreadyTruncated
) {
	if (tag == nullptr || tagLen == 0) {
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

	if (!trace_detail::levelEnabled(level, config.minLevel)) {
		return TraceResult::success("log filtered");
	}

	bool truncated = alreadyTruncated;
	TraceRecord record;
	record.level = level;
	record.uptimeMs = millis();
	copyToRecordField(
	    tag,
	    tagLen,
	    config.maxTagLength,
	    record.tag,
	    sizeof(record.tag),
	    truncated
	);
	copyToRecordField(
	    message,
	    messageLen,
	    config.maxMessageLength,
	    record.message,
	    sizeof(record.message),
	    truncated
	);
	record.truncated = truncated;
	return _impl->appendLog(record);
}

TraceResult Trace::logJson(TraceLevel level, const char *tag, const JsonDocument &doc) {
	TraceJsonFormat format = TraceJsonFormat::Compact;
	size_t maxFormattedLength = TraceConfig().maxFormattedLength;
	{
		TraceLock lock(_impl->mutex);
		if (lock) {
			format = _impl->config.jsonFormat;
			maxFormattedLength = _impl->config.maxFormattedLength;
		}
	}
	const size_t effective = trace_detail::effectiveLimit(
	    maxFormattedLength,
	    TRACE_FORMATTED_BUFFER_LENGTH
	);
	const size_t measuredLength =
	    format == TraceJsonFormat::Pretty ? measureJsonPretty(doc) : measureJson(doc);
	const size_t boundedLength = std::min(measuredLength, effective);
	const bool truncated = measuredLength > effective;
	char buffer[TRACE_FORMATTED_BUFFER_LENGTH + 1] = {};
	const size_t written = format == TraceJsonFormat::Pretty
	                           ? serializeJsonPretty(doc, buffer, boundedLength + 1)
	                           : serializeJson(doc, buffer, boundedLength + 1);
	buffer[boundedLength] = '\0';
	const size_t messageLen = std::min(written, boundedLength);
	const size_t tagLimit = getMaxTagLength();
	const size_t tagLen = boundedStrLen(tag, tagLimit + 1);
	return logRaw(level, tag, tagLen, buffer, messageLen, truncated);
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
	const size_t maxFormattedLength = getMaxFormattedLength();
	char buffer[TRACE_FORMATTED_BUFFER_LENGTH + 1] = {};
	va_list measureArgs;
	va_copy(measureArgs, args);
	const int needed = vsnprintf(nullptr, 0, format, measureArgs);
	va_end(measureArgs);
	if (needed < 0) {
		return TraceResult::failure(TraceStatus::InvalidArgument, "format failed");
	}
	const size_t outputLength = static_cast<size_t>(needed);
	const size_t boundedLength = std::min(outputLength, maxFormattedLength);
	va_list formatArgs;
	va_copy(formatArgs, args);
	vsnprintf(buffer, boundedLength + 1, format, formatArgs);
	va_end(formatArgs);
	const size_t tagLimit = getMaxTagLength();
	const size_t tagLen = boundedStrLen(tag, tagLimit + 1);
	return logRaw(level, tag, tagLen, buffer, boundedLength, outputLength > maxFormattedLength);
}

TraceResult Trace::debug(const char *tag, const char *message) {
	const size_t tagLen = boundedStrLen(tag, getMaxTagLength() + 1);
	const char *safeMessage = message != nullptr ? message : "";
	const size_t messageLen = boundedStrLen(safeMessage, getMaxMessageLength() + 1);
	return logRaw(TraceLevel::Debug, tag, tagLen, safeMessage, messageLen, false);
}

TraceResult Trace::info(const char *tag, const char *message) {
	const size_t tagLen = boundedStrLen(tag, getMaxTagLength() + 1);
	const char *safeMessage = message != nullptr ? message : "";
	const size_t messageLen = boundedStrLen(safeMessage, getMaxMessageLength() + 1);
	return logRaw(TraceLevel::Info, tag, tagLen, safeMessage, messageLen, false);
}

TraceResult Trace::warn(const char *tag, const char *message) {
	const size_t tagLen = boundedStrLen(tag, getMaxTagLength() + 1);
	const char *safeMessage = message != nullptr ? message : "";
	const size_t messageLen = boundedStrLen(safeMessage, getMaxMessageLength() + 1);
	return logRaw(TraceLevel::Warn, tag, tagLen, safeMessage, messageLen, false);
}

TraceResult Trace::error(const char *tag, const char *message) {
	const size_t tagLen = boundedStrLen(tag, getMaxTagLength() + 1);
	const char *safeMessage = message != nullptr ? message : "";
	const size_t messageLen = boundedStrLen(safeMessage, getMaxMessageLength() + 1);
	return logRaw(TraceLevel::Error, tag, tagLen, safeMessage, messageLen, false);
}

TraceResult Trace::fatal(const char *tag, const char *message) {
	const size_t tagLen = boundedStrLen(tag, getMaxTagLength() + 1);
	const char *safeMessage = message != nullptr ? message : "";
	const size_t messageLen = boundedStrLen(safeMessage, getMaxMessageLength() + 1);
	return logRaw(TraceLevel::Fatal, tag, tagLen, safeMessage, messageLen, false);
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

#include "internal/TraceImpl.h"

TraceResult TraceImpl::appendLog(TraceLog log) {
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
				addRealtimeLocked(log);
				lastLogAtMs = log.uptimeMs;
				if (log.truncated) {
					truncatedLogCount++;
				}
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
			if (config.flushOnError && trace_detail::isErrorLevel(log.level)) {
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

void TraceImpl::addRecentLocked(const TraceLog &log) {
	if (config.maxRecentLogs == 0) {
		return;
	}
	while (recentLogs.size() >= config.maxRecentLogs) {
		recentLogs.erase(recentLogs.begin());
	}
	recentLogs.push_back(log);
}

void TraceImpl::addRealtimeLocked(const TraceLog &log) {
	if (config.maxRealtimeLogs == 0 || (!onLog && stream == nullptr)) {
		return;
	}
	while (realtimeLogs.size() >= config.maxRealtimeLogs) {
		realtimeLogs.erase(realtimeLogs.begin());
		droppedRealtimeLogCount++;
	}
	realtimeLogs.push_back(log);
	realtimeLogCount++;
}

void TraceImpl::processRealtimeLogs() {
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
			realtimeLogs.clear();
			return;
		}
		logs.swap(realtimeLogs);
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

size_t Trace::getMaxFormattedLength() const {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TraceConfig().maxFormattedLength;
	}
	return _impl->config.maxFormattedLength;
}

TraceResult Trace::log(TraceLevel level, const char *tag, const std::string &message) {
	TraceConfig config;
	{
		TraceLock lock(_impl->mutex);
		if (lock) {
			config = _impl->config;
		}
	}
	bool messageTruncated = false;
	const std::string boundedMessage =
	    trace_detail::truncateString(message, config.maxMessageLength, messageTruncated);
	return log(level, tag, boundedMessage, messageTruncated);
}

TraceResult Trace::log(
    TraceLevel level,
    const char *tag,
    const std::string &message,
    bool messageTruncated
) {
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

	if (!trace_detail::levelEnabled(level, config.minLevel)) {
		return TraceResult::success("log filtered");
	}

	bool tagTruncated = false;
	TraceLog log;
	log.level = level;
	log.tag = trace_detail::copyLimited(tag, config.maxTagLength, tagTruncated);
	log.message = message;
	log.uptimeMs = millis();
	log.truncated = tagTruncated || messageTruncated;
	return _impl->appendLog(log);
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
	bool truncated = false;
	const std::string message = trace_detail::jsonToString(doc, format, maxFormattedLength, truncated);
	return log(level, tag, message, truncated);
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
	bool truncated = false;
	const std::string message =
	    trace_detail::formatPrintf(format, args, maxFormattedLength, truncated);
	return log(level, tag, message, truncated);
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

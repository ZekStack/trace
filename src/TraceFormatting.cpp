#include "internal/TraceImpl.h"

#include <cstring>
#include <vector>

#if __has_include(<Tempo.h>)
#include <Tempo.h>
#define TRACE_HAS_TEMPO 1
#else
#define TRACE_HAS_TEMPO 0
#endif

namespace trace_detail {
bool levelEnabled(TraceLevel level, TraceLevel minLevel) {
	return static_cast<uint8_t>(level) >= static_cast<uint8_t>(minLevel);
}

bool isErrorLevel(TraceLevel level) {
	return level == TraceLevel::Error || level == TraceLevel::Fatal;
}

size_t effectiveLimit(size_t limit, size_t maximum) {
	if (limit == 0 || limit > maximum) {
		return maximum;
	}
	return limit;
}

size_t clampedLimit(size_t length, size_t limit) {
	if (limit == 0 || length <= limit) {
		return length;
	}
	return limit;
}

std::string copyLimited(const char *value, size_t limit, bool &truncated) {
	if (value == nullptr) {
		return std::string();
	}
	const size_t length = strlen(value);
	const size_t boundedLength = clampedLimit(length, limit);
	truncated = limit > 0 && length > limit;
	return std::string(value, boundedLength);
}

std::string truncateString(const std::string &value, size_t limit, bool &truncated) {
	const size_t boundedLength = clampedLimit(value.size(), limit);
	truncated = limit > 0 && value.size() > limit;
	return value.substr(0, boundedLength);
}

std::string formatPrintf(const char *format, va_list args, size_t limit, bool &truncated) {
	if (format == nullptr) {
		return std::string();
	}

	char stackBuffer[kFormatBufferSize + 1] = {};
	const size_t effective = effectiveLimit(limit, TRACE_FORMATTED_BUFFER_LENGTH);
	va_list copy;
	va_copy(copy, args);
	const int needed = vsnprintf(stackBuffer, effective + 1, format, copy);
	va_end(copy);

	if (needed < 0) {
		return std::string();
	}
	const size_t outputLength = static_cast<size_t>(needed);
	const size_t boundedLength = std::min(outputLength, effective);
	truncated = outputLength > effective;
	return std::string(stackBuffer, boundedLength);
}

std::string jsonToString(
    const JsonDocument &doc,
    TraceJsonFormat format,
    size_t limit,
    bool &truncated
) {
	const size_t measuredLength =
	    format == TraceJsonFormat::Pretty ? measureJsonPretty(doc) : measureJson(doc);
	const size_t effective = effectiveLimit(limit, TRACE_FORMATTED_BUFFER_LENGTH);
	const size_t boundedLength = std::min(measuredLength, effective);
	truncated = measuredLength > effective;

	char buffer[TRACE_FORMATTED_BUFFER_LENGTH + 1] = {};
	if (format == TraceJsonFormat::Pretty) {
		serializeJsonPretty(doc, buffer, boundedLength + 1);
	} else {
		serializeJson(doc, buffer, boundedLength + 1);
	}
	buffer[boundedLength] = '\0';
	return std::string(buffer);
}
} // namespace trace_detail

TraceLog TraceImpl::toPublicLog(const TraceRecord &record) {
	TraceLog log;
	log.sequence = record.sequence;
	log.level = record.level;
	log.tag = record.tag;
	log.message = record.message;
	log.uptimeMs = record.uptimeMs;
	log.truncated = record.truncated;
	return log;
}

void TraceImpl::formatLog(TraceLog &log) {
	char timeBuffer[trace_detail::kTimeBufferSize] = {};
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

bool TraceImpl::formatTempoTime(
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

const char *TraceImpl::levelName(TraceLevel level) {
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

const char *TraceImpl::levelColor(TraceLevel level) {
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

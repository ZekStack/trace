#include "internal/TraceImpl.h"

TraceDiag Trace::getDiagnostics() {
	TraceDiag diag;
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return diag;
	}
	diag.recentLogCount = _impl->recentLogs.size();
	diag.realtimeLogCount = _impl->realtimeLogCount;
	diag.pendingLogCount = _impl->pendingLogs.size();
	diag.droppedLogCount = _impl->droppedLogCount;
	diag.droppedRealtimeLogCount = _impl->droppedRealtimeLogCount;
	diag.truncatedLogCount = _impl->truncatedLogCount;
	diag.flushSuccessCount = _impl->flushSuccessCount;
	diag.flushFailCount = _impl->flushFailCount;
	diag.flushRetryCount = _impl->flushRetryCount;
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

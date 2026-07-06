#include "internal/TraceImpl.h"

#include <cstring>

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
	diag.recentAllocatedBytes = _impl->recentLogs.allocatedBytes();
	diag.realtimeAllocatedBytes = _impl->realtimeLogs.allocatedBytes();
	diag.pendingAllocatedBytes = _impl->pendingLogs.allocatedBytes();
	diag.recentLogsInPsram = _impl->recentLogs.usingPsram();
	diag.realtimeLogsInPsram = _impl->realtimeLogs.usingPsram();
	diag.pendingLogsInPsram = _impl->pendingLogs.usingPsram();
	return diag;
}

TraceLog Trace::getLastLog() {
	TraceLog log;
	{
		TraceLock lock(_impl->mutex);
		if (!lock || _impl->recentLogs.empty()) {
			return log;
		}
		TraceRecord record;
		if (!_impl->recentLogs.peek(_impl->recentLogs.size() - 1, record)) {
			return log;
		}
		log = _impl->toPublicLog(record);
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
		logs.reserve(_impl->recentLogs.size());
		for (size_t i = 0; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record)) {
				logs.push_back(_impl->toPublicLog(record));
			}
		}
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
		for (size_t i = 0; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record) && record.level == level) {
				logs.push_back(_impl->toPublicLog(record));
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
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record)) {
				logs.push_back(_impl->toPublicLog(record));
			}
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
		for (size_t i = 0; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record) && strcmp(record.tag, tag) == 0) {
				logs.push_back(_impl->toPublicLog(record));
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

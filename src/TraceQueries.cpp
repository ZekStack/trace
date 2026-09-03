#include "internal/TraceImpl.h"

#include <cstring>

TraceDiag Trace::getDiagnostics() {
	TraceDiag diag;
	if (!_impl) {
		return diag;
	}
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
	diag.stackHighWaterMarkBytes =
	    _impl->task ? _impl->task.stackHighWaterMarkBytes() : _impl->stackHighWaterMarkBytes;
	diag.requestedAllocationPlacement = _impl->config.memory.allocation;
	diag.requestedRealtimeAllocationPlacement = _impl->realtimeAllocationPlacement();
	diag.requestedTaskStackPlacement = _impl->config.memory.taskStack;
	diag.taskStackRegion = _impl->taskStackRegion;
	diag.recentAllocatedBytes = _impl->recentLogs.allocatedBytes();
	diag.realtimeAllocatedBytes = _impl->realtimeLogs.allocatedBytes();
	diag.pendingAllocatedBytes = _impl->pendingLogs.allocatedBytes();
	diag.recentStorageRegion = _impl->recentLogs.region();
	diag.realtimeStorageRegion = _impl->realtimeLogs.region();
	diag.pendingStorageRegion = _impl->pendingLogs.region();
	return diag;
}

TraceLog Trace::getLastLog() {
	if (!_impl) {
		return TraceLog();
	}
	TraceRecord record;
	Strata::Placement placement = Strata::Placement::PreferExternal;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceLog(placement);
		}
		placement = _impl->allocationPlacement();
		if (_impl->recentLogs.empty() ||
		    !_impl->recentLogs.peek(_impl->recentLogs.size() - 1, record)) {
			return TraceLog(placement);
		}
	}
	TraceLog log = _impl->toPublicLog(record, placement);
	_impl->formatLog(log);
	return log;
}

TraceLogList Trace::getLogs() {
	Strata::Placement placement = Strata::Placement::PreferExternal;
	if (_impl) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			placement = _impl->allocationPlacement();
		}
	}
	TraceLogList logs{Strata::Allocator<TraceLog>{placement}};
	if (!_impl) {
		return logs;
	}
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		logs.reserve(_impl->recentLogs.size());
		for (size_t i = 0; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record)) {
				logs.push_back(_impl->toPublicLog(record, placement));
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

TraceLogList Trace::getLogs(TraceLevel level) {
	Strata::Placement placement = Strata::Placement::PreferExternal;
	if (_impl) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			placement = _impl->allocationPlacement();
		}
	}
	TraceLogList logs{Strata::Allocator<TraceLog>{placement}};
	if (!_impl) {
		return logs;
	}
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		for (size_t i = 0; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record) && record.level == level) {
				logs.push_back(_impl->toPublicLog(record, placement));
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

TraceLogList Trace::getLastLogs(size_t count) {
	Strata::Placement placement = Strata::Placement::PreferExternal;
	if (_impl) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			placement = _impl->allocationPlacement();
		}
	}
	TraceLogList logs{Strata::Allocator<TraceLog>{placement}};
	if (!_impl || count == 0) {
		return logs;
	}
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return logs;
		}
		const size_t start = count >= _impl->recentLogs.size() ? 0 : _impl->recentLogs.size() - count;
		for (size_t i = start; i < _impl->recentLogs.size(); ++i) {
			TraceRecord record;
			if (_impl->recentLogs.peek(i, record)) {
				logs.push_back(_impl->toPublicLog(record, placement));
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

TraceLogList Trace::getLogsByTag(const char *tag) {
	Strata::Placement placement = Strata::Placement::PreferExternal;
	if (_impl) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			placement = _impl->allocationPlacement();
		}
	}
	TraceLogList logs{Strata::Allocator<TraceLog>{placement}};
	if (!_impl || tag == nullptr) {
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
				logs.push_back(_impl->toPublicLog(record, placement));
			}
		}
	}
	for (TraceLog &log : logs) {
		_impl->formatLog(log);
	}
	return logs;
}

#include "internal/TraceImpl.h"

#include <algorithm>

uint32_t TraceImpl::retryIntervalMsLocked() const {
	return config.retryIntervalMs < trace_detail::kWaitPollMs ? trace_detail::kWaitPollMs
	                                                         : config.retryIntervalMs;
}

void TraceImpl::performFlush() {
	TraceFlushCallback callback;
	TraceLogBatch batch;
	uint64_t maxSequence = 0;
	{
		TraceLock lock(mutex);
		if (!lock) {
			return;
		}
		callback = onFlush;
		batch.createdAtUptimeMs = millis();
		batch.logs.reserve(pendingLogs.size());
		for (size_t i = 0; i < pendingLogs.size(); ++i) {
			TraceRecord record;
			if (pendingLogs.peek(i, record)) {
				batch.logs.push_back(toPublicLog(record));
			}
		}
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
			nextFlushAttemptMs = 0;
			if (maxSequence > 0) {
				TraceRecord record;
				while (pendingLogs.peek(0, record) && record.sequence <= maxSequence) {
					pendingLogs.pop(record);
				}
			}
			flushSuccessCount++;
		} else if (flushResult == TraceFlushResult::Retry) {
			flushRetryCount++;
			nextFlushAttemptMs = lastFlushAtMs + retryIntervalMsLocked();
		} else {
			nextFlushAttemptMs = 0;
			flushFailCount++;
			if (stopping) {
				shutdownFlushFailed = true;
			}
		}
	}
}

bool TraceImpl::shouldFlushNow() {
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
	const uint64_t nowMs = millis();
	if (nextFlushAttemptMs > 0) {
		return nowMs >= nextFlushAttemptMs;
	}
	if (config.flushEveryLogs > 0 && pendingLogs.size() >= config.flushEveryLogs) {
		return true;
	}
	if (config.flushIntervalMs == 0) {
		return false;
	}
	return lastFlushAtMs == 0 || nowMs - lastFlushAtMs >= config.flushIntervalMs;
}

TickType_t TraceImpl::waitTicks() {
	TraceLock lock(mutex);
	if (!lock) {
		return portMAX_DELAY;
	}
	uint32_t waitMs = 0;
	if (config.flushIntervalMs > 0) {
		waitMs = config.flushIntervalMs;
	}
	if (!pendingLogs.empty() && nextFlushAttemptMs > 0) {
		const uint64_t nowMs = millis();
		const uint32_t retryWaitMs =
		    nowMs >= nextFlushAttemptMs ? 0 : static_cast<uint32_t>(nextFlushAttemptMs - nowMs);
		waitMs = waitMs == 0 ? retryWaitMs : std::min(waitMs, retryWaitMs);
	}
	if (waitMs == 0) {
		return portMAX_DELAY;
	}
	return pdMS_TO_TICKS(waitMs);
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
			if (result == TraceFlushResult::Failed) {
				return TraceResult::failure(TraceStatus::FlushFailed, "flush failed");
			}
		}
		if (millis() - startedMs >= timeoutMs) {
			return TraceResult::failure(TraceStatus::Timeout, "flush timed out");
		}
		vTaskDelay(pdMS_TO_TICKS(trace_detail::kWaitPollMs));
	}
}

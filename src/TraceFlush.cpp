#include "internal/TraceImpl.h"

#include <algorithm>

uint32_t TraceImpl::retryIntervalMsLocked() const {
	return config.retryIntervalMs < trace_detail::kWaitPollMs ? trace_detail::kWaitPollMs
	                                                         : config.retryIntervalMs;
}

uint64_t TraceImpl::latestPendingSequenceLocked() const {
	if (pendingLogs.empty()) {
		return 0;
	}
	TraceRecord record;
	if (!pendingLogs.peek(pendingLogs.size() - 1, record)) {
		return 0;
	}
	return record.sequence;
}

size_t TraceImpl::pendingFlushBatchSizeLocked(uint64_t targetSequence) const {
	if (targetSequence == 0 || pendingLogs.empty()) {
		return 0;
	}
	const size_t configuredLimit = config.maxFlushBatchLogs;
	size_t count = 0;
	for (size_t i = 0; i < pendingLogs.size(); ++i) {
		TraceRecord record;
		if (!pendingLogs.peek(i, record) || record.sequence > targetSequence) {
			break;
		}
		count++;
		if (configuredLimit > 0 && count >= configuredLimit) {
			break;
		}
	}
	return count;
}

void TraceImpl::performFlush() {
	{
		TraceLock lock(mutex);
		if (!lock) {
			return;
		}
		activeFlushTargetSequence = latestPendingSequenceLocked();
		flushRequested = false;
		urgentFlushRequested = false;
		if (activeFlushTargetSequence == 0) {
			lastFlushAtMs = millis();
			lastFlushResult = TraceFlushResult::Ok;
			flushGeneration++;
			return;
		}
	}

	while (true) {
		TraceFlushCallback callback;
		TraceLogBatch batch;
		uint64_t maxSequence = 0;
		{
			TraceLock lock(mutex);
			if (!lock) {
				return;
			}
			const size_t batchSize = pendingFlushBatchSizeLocked(activeFlushTargetSequence);
			if (batchSize == 0) {
				activeFlushTargetSequence = 0;
				return;
			}
			callback = onFlush;
			batch.createdAtUptimeMs = millis();
			batch.logs.reserve(batchSize);
			for (size_t i = 0; i < batchSize; ++i) {
				TraceRecord record;
				if (pendingLogs.peek(i, record)) {
					batch.logs.push_back(toPublicLog(record));
				}
			}
			if (!batch.logs.empty()) {
				maxSequence = batch.logs.back().sequence;
			}
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
					lastFlushedSequence = std::max(lastFlushedSequence, maxSequence);
				}
				flushSuccessCount++;
				if (maxSequence >= activeFlushTargetSequence) {
					activeFlushTargetSequence = 0;
					return;
				}
			} else if (flushResult == TraceFlushResult::Retry) {
				flushRetryCount++;
				nextFlushAttemptMs = lastFlushAtMs + retryIntervalMsLocked();
				activeFlushTargetSequence = 0;
				return;
			} else {
				nextFlushAttemptMs = 0;
				flushFailCount++;
				activeFlushTargetSequence = 0;
				if (stopping) {
					shutdownFlushFailed = true;
				}
				return;
			}
		}
	}
}

bool TraceImpl::shouldFlushNow() {
	TraceLock lock(mutex);
	if (!lock) {
		return false;
	}
	const uint64_t nowMs = millis();
	if (
	    !pendingLogs.empty() && nextFlushAttemptMs > 0 && nowMs < nextFlushAttemptMs &&
	    !urgentFlushRequested
	) {
		return false;
	}
	if (flushRequested || urgentFlushRequested) {
		return true;
	}
	if (pendingLogs.empty()) {
		return false;
	}
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
	uint32_t startGeneration = 0;
	uint64_t targetSequence = 0;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		if (!_impl->initialized || _impl->stopping) {
			return TraceResult::failure(TraceStatus::NotInitialized, "trace is not initialized");
		}
		startGeneration = _impl->flushGeneration;
		targetSequence = _impl->latestPendingSequenceLocked();
		_impl->waitingFlushTargetSequence = targetSequence;
		_impl->flushRequested = true;
	}
	_impl->wakeTask();

	const uint32_t startedMs = millis();
	while (true) {
		TraceFlushResult result = TraceFlushResult::Ok;
		uint32_t generation = 0;
		bool done = false;
		{
			TraceLock lock(_impl->mutex);
			if (lock) {
				generation = _impl->flushGeneration;
				done = _impl->lastFlushedSequence >= targetSequence;
				result = _impl->lastFlushResult;
			}
		}
		if (done) {
			return TraceResult::success("flush completed");
		}
		if (generation > startGeneration && result == TraceFlushResult::Failed) {
			return TraceResult::failure(TraceStatus::FlushFailed, "flush failed");
		}
		if (millis() - startedMs >= timeoutMs) {
			return TraceResult::failure(TraceStatus::Timeout, "flush timed out");
		}
		vTaskDelay(pdMS_TO_TICKS(trace_detail::kWaitPollMs));
	}
}

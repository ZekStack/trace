#include "internal/TraceImpl.h"

bool TraceImpl::initBuffers() {
	deinitBuffers();
	if (!recentLogs.init(config.maxRecentLogs, config.storageMemory, false)) {
		deinitBuffers();
		return false;
	}
	if (!realtimeLogs.init(config.maxRealtimeLogs, config.realtimeStorageMemory, true)) {
		deinitBuffers();
		return false;
	}
	if (!pendingLogs.init(config.maxPendingLogs, config.storageMemory, false)) {
		deinitBuffers();
		return false;
	}
	return true;
}

void TraceImpl::deinitBuffers() {
	recentLogs.deinit();
	realtimeLogs.deinit();
	pendingLogs.deinit();
}

void TraceImpl::wakeTask() {
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

bool TraceImpl::isStopping() {
	TraceLock lock(mutex);
	return lock && stopping;
}

bool TraceImpl::shouldStopForShutdown() {
	TraceLock lock(mutex);
	if (!lock || !stopping) {
		return false;
	}
	if (shutdownFlushFailed || pendingLogs.empty()) {
		return true;
	}
	if (shutdownDeadlineMs > 0 && millis() >= shutdownDeadlineMs) {
		shutdownTimedOut = true;
		return true;
	}
	return false;
}

TickType_t TraceImpl::shutdownWaitTicks() {
	TickType_t ticks = waitTicks();
	TraceLock lock(mutex);
	if (!lock || shutdownDeadlineMs == 0) {
		return ticks;
	}
	const uint64_t nowMs = millis();
	if (nowMs >= shutdownDeadlineMs) {
		return 0;
	}
	const TickType_t deadlineTicks =
	    pdMS_TO_TICKS(static_cast<uint32_t>(shutdownDeadlineMs - nowMs));
	if (ticks == portMAX_DELAY || deadlineTicks < ticks) {
		return deadlineTicks;
	}
	return ticks;
}

void TraceImpl::markTaskStopped() {
	TraceLock lock(mutex);
	if (lock) {
		stackHighWaterMarkBytes = trace_task_support::currentStackHighWaterMarkBytes();
		taskHandle = nullptr;
	}
}

void TraceImpl::taskEntry(void *arg) {
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
				continue;
			}
			if (impl->shouldStopForShutdown()) {
				break;
			}
			ulTaskNotifyTake(pdTRUE, impl->shutdownWaitTicks());
			continue;
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
		_impl->nextSequence = 1;
		_impl->droppedLogCount = 0;
		_impl->realtimeLogCount = 0;
		_impl->droppedRealtimeLogCount = 0;
		_impl->truncatedLogCount = 0;
		_impl->flushSuccessCount = 0;
		_impl->flushFailCount = 0;
		_impl->flushRetryCount = 0;
		_impl->lastFlushAtMs = 0;
		_impl->lastLogAtMs = 0;
		_impl->stackHighWaterMarkBytes = 0;
		_impl->flushGeneration = 0;
		_impl->lastFlushResult = TraceFlushResult::Ok;
		_impl->lastAcceptedPendingSequence = 0;
		_impl->lastFlushedSequence = 0;
		_impl->activeFlushTargetSequence = 0;
		_impl->waitingFlushTargetSequence = 0;
		_impl->nextFlushAttemptMs = 0;
		_impl->shutdownDeadlineMs = 0;
		_impl->shutdownTimedOut = false;
		_impl->shutdownFlushFailed = false;
		if (!_impl->initBuffers()) {
			return TraceResult::failure(
			    TraceStatus::OutOfMemory,
			    "failed to allocate trace buffers"
			);
		}
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
		TraceLock lock(_impl->mutex);
		if (lock) {
			_impl->deinitBuffers();
		}
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
		_impl->shutdownDeadlineMs = millis() + timeoutMs;
		_impl->shutdownTimedOut = false;
		_impl->shutdownFlushFailed = false;
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
				const bool timedOut = _impl->shutdownTimedOut;
				const bool flushFailed = _impl->shutdownFlushFailed;
				_impl->initialized = false;
				_impl->stopping = false;
				_impl->shutdownDeadlineMs = 0;
				_impl->deinitBuffers();
				if (timedOut) {
					return TraceResult::failure(TraceStatus::Timeout, "trace end timed out");
				}
				if (flushFailed) {
					return TraceResult::failure(TraceStatus::FlushFailed, "trace end flush failed");
				}
				return TraceResult::success("trace ended");
			}
		}
		const uint32_t elapsedMs = millis() - startedMs;
		if (elapsedMs >= timeoutMs) {
			TraceLock lock(_impl->mutex);
			if (lock) {
				_impl->shutdownTimedOut = true;
			}
			if (elapsedMs >= timeoutMs + (trace_detail::kWaitPollMs * 4)) {
				return TraceResult::failure(TraceStatus::Timeout, "trace end timed out");
			}
		}
		vTaskDelay(pdMS_TO_TICKS(trace_detail::kWaitPollMs));
	}
}

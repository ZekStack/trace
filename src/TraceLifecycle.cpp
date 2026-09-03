#include "internal/TraceImpl.h"

#include <limits>

namespace {
bool isValidStackSize(size_t stackBytes) {
	return stackBytes >= trace_detail::kMinStackSizeBytes &&
	       (stackBytes % sizeof(StackType_t)) == 0;
}

[[noreturn]] void suspendForever() {
	vTaskSuspend(nullptr);
	for (;;) {
		vTaskDelay(portMAX_DELAY);
	}
}
} // namespace

bool TraceImpl::initBuffers() {
	deinitBuffers();
	const Strata::Placement allocation = allocationPlacement();
	const Strata::Placement realtimeAllocation = realtimeAllocationPlacement();
	if (!recentLogs.init(config.maxRecentLogs, allocation)) {
		deinitBuffers();
		return false;
	}
	if (!realtimeLogs.init(config.maxRealtimeLogs, realtimeAllocation)) {
		deinitBuffers();
		return false;
	}
	if (!pendingLogs.init(config.maxPendingLogs, allocation)) {
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
			handle = task.handle();
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

void TraceImpl::markTaskReadyForDelete() {
	{
		TraceLock lock(mutex);
		if (lock) {
			stackHighWaterMarkBytes = task.stackHighWaterMarkBytes();
		}
	}
	taskReadyForDelete.store(true, std::memory_order_release);
}

void TraceImpl::taskEntry(void *arg) {
	TraceImpl *impl = static_cast<TraceImpl *>(arg);
	if (impl == nullptr) {
		suspendForever();
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

	impl->markTaskReadyForDelete();
	suspendForever();
}

TraceResult Trace::init(const TraceConfig &config) {
	if (!_impl || !_impl->mutex) {
		return TraceResult::failure(TraceStatus::OutOfMemory, "failed to allocate trace state");
	}
	if (!isValidStackSize(config.stackSize)) {
		return TraceResult::failure(
		    TraceStatus::InvalidArgument,
		    "stack size must be at least 1024 bytes and aligned"
		);
	}
	if (config.taskName == nullptr || config.taskName[0] == '\0') {
		return TraceResult::failure(TraceStatus::InvalidArgument, "task name is required");
	}
	if (!Strata::validMemoryPolicy(config.memory)) {
		return TraceResult::failure(TraceStatus::InvalidArgument, "invalid memory policy");
	}
	if (config.realtimeAllocation.has_value() &&
	    !Strata::validPlacement(*config.realtimeAllocation)) {
		return TraceResult::failure(
		    TraceStatus::InvalidArgument,
		    "invalid realtime allocation placement"
		);
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
		_impl->stopping = false;
		_impl->flushRequested = false;
		_impl->urgentFlushRequested = false;
		_impl->taskReadyForDelete.store(false, std::memory_order_relaxed);
		_impl->taskStackRegion = Strata::Region::Unknown;
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

		// Callbacks may be registered before init(), when only the default policy is known.
		// Rebuild the Strata-owned holders after applying the requested configuration so
		// runtime callback ownership follows the same placement contract as other movable data.
		if (_impl->onFlush) {
			_impl->onFlush = Strata::makeShared<TraceFlushCallback>(
			    _impl->allocationPlacement(),
			    *_impl->onFlush
			);
		}
		if (_impl->onLog) {
			_impl->onLog = Strata::makeShared<TraceLogCallback>(
			    _impl->realtimeAllocationPlacement(),
			    *_impl->onLog
			);
		}
	}

	Strata::FreeRTOS::Task task = Strata::FreeRTOS::Task::create(
	    &TraceImpl::taskEntry,
	    _impl.get(),
	    Strata::FreeRTOS::TaskConfig{
	        .name = config.taskName,
	        .stackBytes = config.stackSize,
	        .stackPlacement = config.memory.taskStack,
	        .priority = config.priority,
	        .affinity = static_cast<std::int32_t>(config.coreId),
	    }
	);
	if (!task) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			_impl->deinitBuffers();
		}
		return TraceResult::failure(TraceStatus::TaskCreateFailed, "failed to create trace task");
	}

	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			TaskHandle_t handle = task.handle();
			if (handle != nullptr) {
				vTaskSuspend(handle);
			}
			task.reset();
			_impl->deinitBuffers();
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		_impl->taskStackRegion = task.stackRegion();
		_impl->task = std::move(task);
		_impl->initialized = true;
	}

	return TraceResult::success("trace initialized");
}

TraceResult Trace::end(uint32_t timeoutMs) {
	if (!_impl) {
		return TraceResult::success("trace is not initialized");
	}

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
		_impl->shutdownDeadlineMs =
		    timeoutMs == UINT32_MAX ? 0 : static_cast<uint64_t>(millis()) + timeoutMs;
		_impl->shutdownTimedOut = false;
		_impl->shutdownFlushFailed = false;
		handle = _impl->task.handle();
	}
	if (handle != nullptr) {
		xTaskNotifyGive(handle);
	}

	const uint32_t startedMs = millis();
	while (!_impl->taskReadyForDelete.load(std::memory_order_acquire)) {
		if (timeoutMs != UINT32_MAX && static_cast<uint32_t>(millis() - startedMs) >= timeoutMs) {
			return TraceResult::failure(TraceStatus::Timeout, "trace end timed out");
		}
		vTaskDelay(pdMS_TO_TICKS(trace_detail::kWaitPollMs));
	}

	bool timedOut = false;
	bool flushFailed = false;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		timedOut = _impl->shutdownTimedOut;
		flushFailed = _impl->shutdownFlushFailed;
		handle = _impl->task.handle();
	}

	if (handle != nullptr) {
		vTaskSuspend(handle);
	}
	_impl->task.reset();

	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
		}
		_impl->initialized = false;
		_impl->stopping = false;
		_impl->shutdownDeadlineMs = 0;
		_impl->taskReadyForDelete.store(false, std::memory_order_relaxed);
		_impl->deinitBuffers();
	}

	if (timedOut) {
		return TraceResult::failure(TraceStatus::Timeout, "trace end timed out");
	}
	if (flushFailed) {
		return TraceResult::failure(TraceStatus::FlushFailed, "trace end flush failed");
	}
	return TraceResult::success("trace ended");
}

#pragma once

#include "FreeRTOS.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

extern std::atomic<uint64_t> trace_host_millis;

using TaskFunction_t = void (*)(void *);

namespace trace_host {
struct TaskControl {
	std::mutex mutex;
	std::condition_variable condition;
	uint32_t notifications = 0;
};

inline thread_local TaskControl *currentTask = nullptr;
} // namespace trace_host

using TaskHandle_t = trace_host::TaskControl *;

inline BaseType_t xTaskCreate(
    TaskFunction_t entry,
    const char *name,
    uint32_t stackDepth,
    void *arg,
    UBaseType_t priority,
    TaskHandle_t *handle
) {
	(void)name;
	(void)stackDepth;
	(void)priority;
	if (entry == nullptr || handle == nullptr) {
		return pdFAIL;
	}
	auto *control = new trace_host::TaskControl();
	*handle = control;
	std::thread([entry, arg, control]() {
		trace_host::currentTask = control;
		entry(arg);
		trace_host::currentTask = nullptr;
	}).detach();
	return pdPASS;
}

inline BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t entry,
    const char *name,
    uint32_t stackDepth,
    void *arg,
    UBaseType_t priority,
    TaskHandle_t *handle,
    BaseType_t coreId
) {
	(void)coreId;
	return xTaskCreate(entry, name, stackDepth, arg, priority, handle);
}

inline void xTaskNotifyGive(TaskHandle_t handle) {
	if (handle == nullptr) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(handle->mutex);
		handle->notifications++;
	}
	handle->condition.notify_one();
}

inline uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticksToWait) {
	auto *control = trace_host::currentTask;
	if (control == nullptr) {
		return 0;
	}
	std::unique_lock<std::mutex> lock(control->mutex);
	if (control->notifications == 0) {
		if (ticksToWait == portMAX_DELAY) {
			control->condition.wait(lock, [control]() { return control->notifications > 0; });
		} else if (ticksToWait > 0) {
			lock.unlock();
			trace_host_millis.fetch_add(ticksToWait);
			std::this_thread::yield();
			lock.lock();
		}
	}
	const uint32_t value = control->notifications;
	if (clearOnExit == pdTRUE) {
		control->notifications = 0;
	} else if (control->notifications > 0) {
		control->notifications--;
	}
	return value;
}

inline void vTaskDelay(TickType_t ticks) {
	trace_host_millis.fetch_add(ticks);
	std::this_thread::yield();
}

inline void vTaskDelete(TaskHandle_t handle) {
	(void)handle;
}

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
	return trace_host::currentTask;
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
	(void)handle;
	return 4096;
}

#pragma once

#include "FreeRTOS.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>

extern std::atomic<uint64_t> trace_host_millis;
extern std::atomic<int> trace_host_active_tasks;

using TaskFunction_t = void (*)(void *);

namespace trace_host {
struct TaskDeleted final : std::exception {};

struct TaskControl {
	std::mutex mutex;
	std::condition_variable condition;
	uint32_t notifications = 0;
	bool deleted = false;
	std::thread thread;
};

inline thread_local TaskControl *currentTask = nullptr;
} // namespace trace_host

using TaskHandle_t = trace_host::TaskControl *;

inline TaskHandle_t xTaskCreateStatic(
    TaskFunction_t entry,
    const char *name,
    configSTACK_DEPTH_TYPE stackDepth,
    void *arg,
    UBaseType_t priority,
    StackType_t *stackBuffer,
    StaticTask_t *taskBuffer
) {
	(void)name;
	(void)stackDepth;
	(void)priority;
	(void)stackBuffer;
	(void)taskBuffer;
	if (entry == nullptr) {
		return nullptr;
	}
	auto *control = new trace_host::TaskControl();
	trace_host_active_tasks.fetch_add(1, std::memory_order_relaxed);
	control->thread = std::thread([entry, arg, control]() {
		trace_host::currentTask = control;
		try {
			entry(arg);
		} catch (const trace_host::TaskDeleted &) {
		}
		trace_host::currentTask = nullptr;
	});
	return control;
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
	if (control->notifications == 0 && !control->deleted) {
		if (ticksToWait == portMAX_DELAY) {
			control->condition.wait(lock, [control]() {
				return control->notifications > 0 || control->deleted;
			});
		} else if (ticksToWait > 0) {
			lock.unlock();
			trace_host_millis.fetch_add(ticksToWait);
			std::this_thread::yield();
			lock.lock();
		}
	}
	if (control->deleted) {
		throw trace_host::TaskDeleted();
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

inline void vTaskSuspend(TaskHandle_t handle) {
	if (handle != nullptr) {
		return;
	}
	auto *control = trace_host::currentTask;
	if (control == nullptr) {
		return;
	}
	std::unique_lock<std::mutex> lock(control->mutex);
	control->condition.wait(lock, [control]() { return control->deleted; });
	throw trace_host::TaskDeleted();
}

inline void vTaskDelete(TaskHandle_t handle) {
	if (handle == nullptr) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(handle->mutex);
		handle->deleted = true;
	}
	handle->condition.notify_all();
	if (handle->thread.joinable()) {
		handle->thread.join();
	}
	delete handle;
	trace_host_active_tasks.fetch_sub(1, std::memory_order_relaxed);
}

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
	return trace_host::currentTask;
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
	(void)handle;
	return 1024;
}

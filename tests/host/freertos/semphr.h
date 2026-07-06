#pragma once

#include "FreeRTOS.h"

#include <mutex>

using SemaphoreHandle_t = std::recursive_mutex *;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
	return new std::recursive_mutex();
}

inline void vSemaphoreDelete(SemaphoreHandle_t handle) {
	delete handle;
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t timeout) {
	(void)timeout;
	if (handle == nullptr) {
		return pdFALSE;
	}
	handle->lock();
	return pdTRUE;
}

inline void xSemaphoreGiveRecursive(SemaphoreHandle_t handle) {
	if (handle != nullptr) {
		handle->unlock();
	}
}

#pragma once

#include "FreeRTOS.h"

#include <memory>

using SemaphoreHandle_t = std::recursive_mutex *;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage) {
	if (storage == nullptr) {
		return nullptr;
	}
	auto *mutex = std::construct_at(
	    reinterpret_cast<std::recursive_mutex *>(storage->storage)
	);
	storage->constructed = true;
	return mutex;
}

inline void vSemaphoreDelete(SemaphoreHandle_t handle) {
	if (handle != nullptr) {
		std::destroy_at(handle);
	}
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t timeout) {
	(void)timeout;
	if (handle == nullptr) {
		return pdFALSE;
	}
	handle->lock();
	return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t handle) {
	if (handle == nullptr) {
		return pdFALSE;
	}
	handle->unlock();
	return pdTRUE;
}

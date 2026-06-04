#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class TraceMutex {
  public:
	TraceMutex() {
		_handle = xSemaphoreCreateRecursiveMutex();
	}

	~TraceMutex() {
		if (_handle != nullptr) {
			vSemaphoreDelete(_handle);
		}
	}

	TraceMutex(const TraceMutex &) = delete;
	TraceMutex &operator=(const TraceMutex &) = delete;

	bool lock(TickType_t timeout = portMAX_DELAY) {
		return _handle != nullptr && xSemaphoreTakeRecursive(_handle, timeout) == pdTRUE;
	}

	void unlock() {
		if (_handle != nullptr) {
			xSemaphoreGiveRecursive(_handle);
		}
	}

  private:
	SemaphoreHandle_t _handle = nullptr;
};

class TraceLock {
  public:
	explicit TraceLock(TraceMutex &mutex) : _mutex(mutex), _locked(mutex.lock()) {
	}

	~TraceLock() {
		if (_locked) {
			_mutex.unlock();
		}
	}

	TraceLock(const TraceLock &) = delete;
	TraceLock &operator=(const TraceLock &) = delete;

	explicit operator bool() const {
		return _locked;
	}

  private:
	TraceMutex &_mutex;
	bool _locked = false;
};

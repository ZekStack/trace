#pragma once

#include <strata/freertos/Mutex.h>

class TraceLock {
  public:
	explicit TraceLock(Strata::FreeRTOS::RecursiveMutex &mutex)
	    : _mutex(mutex), _locked(mutex.lock()) {
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
	Strata::FreeRTOS::RecursiveMutex &_mutex;
	bool _locked = false;
};

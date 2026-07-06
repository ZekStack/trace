#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern std::atomic<uint64_t> trace_host_millis;

inline uint32_t millis() {
	return static_cast<uint32_t>(trace_host_millis.load());
}

inline void delay(uint32_t ms) {
	trace_host_millis.fetch_add(ms);
	std::this_thread::yield();
}

class Print {
  public:
	virtual ~Print() = default;

	virtual size_t write(uint8_t value) {
		(void)value;
		return 1;
	}

	size_t print(const char *value) {
		if (value == nullptr) {
			return 0;
		}
		const size_t length = strlen(value);
		for (size_t i = 0; i < length; ++i) {
			write(static_cast<uint8_t>(value[i]));
		}
		return length;
	}

	size_t println(const char *value) {
		const size_t written = print(value);
		write('\n');
		return written + 1;
	}
};

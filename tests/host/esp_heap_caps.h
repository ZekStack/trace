#pragma once

#include <cstdlib>
#include <cstddef>
#include <unordered_map>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4

namespace trace_host_heap {
inline bool psramAvailable = false;
inline bool failNextAllocation = false;
inline int failCaps = 0;
inline int allocationsBeforeFailure = -1;
inline size_t allocationCount = 0;
inline size_t freeCount = 0;
inline size_t activeAllocations = 0;
inline std::unordered_map<void *, int> allocationCaps;

inline void reset() {
	psramAvailable = false;
	failNextAllocation = false;
	failCaps = 0;
	allocationsBeforeFailure = -1;
	allocationCount = 0;
	freeCount = 0;
	activeAllocations = 0;
	allocationCaps.clear();
}
} // namespace trace_host_heap

inline size_t heap_caps_get_total_size(int caps) {
	if ((caps & MALLOC_CAP_SPIRAM) != 0 && trace_host_heap::psramAvailable) {
		return 4 * 1024 * 1024;
	}
	return 0;
}

inline void *heap_caps_malloc(size_t size, int caps) {
	if (trace_host_heap::failNextAllocation &&
	    (trace_host_heap::failCaps == 0 || (caps & trace_host_heap::failCaps) != 0)) {
		trace_host_heap::failNextAllocation = false;
		return nullptr;
	}
	if (trace_host_heap::allocationsBeforeFailure >= 0 &&
	    (trace_host_heap::failCaps == 0 || (caps & trace_host_heap::failCaps) != 0)) {
		if (trace_host_heap::allocationsBeforeFailure == 0) {
			return nullptr;
		}
		trace_host_heap::allocationsBeforeFailure--;
	}
	void *ptr = std::malloc(size);
	if (ptr == nullptr) {
		return nullptr;
	}
	trace_host_heap::allocationCount++;
	trace_host_heap::activeAllocations++;
	trace_host_heap::allocationCaps[ptr] = caps;
	return ptr;
}

inline void heap_caps_free(void *ptr) {
	if (ptr == nullptr) {
		return;
	}
	trace_host_heap::freeCount++;
	if (trace_host_heap::activeAllocations > 0) {
		trace_host_heap::activeAllocations--;
	}
	trace_host_heap::allocationCaps.erase(ptr);
	std::free(ptr);
}

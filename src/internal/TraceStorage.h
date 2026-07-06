#pragma once

#include "../Trace.h"

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "esp_heap_caps.h"

struct TraceRecord {
	uint64_t sequence = 0;
	uint64_t uptimeMs = 0;
	uint32_t epochSeconds = 0;
	TraceLevel level = TraceLevel::Info;
	bool hasEpochTime = false;
	bool truncated = false;
	char tag[TRACE_RECORD_MAX_TAG_LENGTH + 1] = {};
	char message[TRACE_RECORD_MAX_MESSAGE_LENGTH + 1] = {};
};

namespace trace_storage {
#if defined(MALLOC_CAP_INTERNAL)
inline constexpr int kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#else
inline constexpr int kInternalCaps = MALLOC_CAP_8BIT;
#endif

#if defined(MALLOC_CAP_SPIRAM)
inline constexpr int kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
inline constexpr int kPsramCaps = MALLOC_CAP_8BIT;
#endif

struct AllocationInfo {
	size_t bytes = 0;
	bool psram = false;
};

inline bool psramAvailable() {
#if defined(MALLOC_CAP_SPIRAM)
	return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
	return false;
#endif
}

inline void *allocate(
    size_t bytes,
    TraceStorageMemory memory,
    bool realtime,
    AllocationInfo &info
) {
	info = AllocationInfo();
	if (bytes == 0) {
		return nullptr;
	}

	void *ptr = nullptr;
	if (!realtime && memory != TraceStorageMemory::Internal) {
#if defined(MALLOC_CAP_SPIRAM)
		if (psramAvailable()) {
			ptr = heap_caps_malloc(bytes, kPsramCaps);
			if (ptr != nullptr) {
				info.bytes = bytes;
				info.psram = true;
				return ptr;
			}
		}
#endif
		if (memory == TraceStorageMemory::RequirePsram) {
			return nullptr;
		}
	}

	ptr = heap_caps_malloc(bytes, kInternalCaps);
	if (ptr != nullptr) {
		info.bytes = bytes;
		info.psram = false;
	}
	return ptr;
}

inline void deallocate(void *ptr) {
	if (ptr != nullptr) {
		heap_caps_free(ptr);
	}
}
} // namespace trace_storage

template <typename T>
class TraceRingBuffer {
  public:
	TraceRingBuffer() = default;
	~TraceRingBuffer() {
		deinit();
	}

	TraceRingBuffer(const TraceRingBuffer &) = delete;
	TraceRingBuffer &operator=(const TraceRingBuffer &) = delete;

	bool init(size_t capacity, TraceStorageMemory memoryType, bool realtime) {
		static_assert(std::is_trivially_copyable<T>::value, "TraceRingBuffer requires trivial items");
		static_assert(
		    std::is_trivially_destructible<T>::value,
		    "TraceRingBuffer requires trivially destructible items"
		);
		deinit();
		if (capacity == 0) {
			return true;
		}
		const size_t bytes = sizeof(T) * capacity;
		trace_storage::AllocationInfo allocation;
		void *items = trace_storage::allocate(bytes, memoryType, realtime, allocation);
		if (items == nullptr) {
			return false;
		}
		items_ = static_cast<T *>(items);
		capacity_ = capacity;
		allocation_ = allocation;
		clearStorage();
		return true;
	}

	void deinit() {
		if (items_ != nullptr) {
			for (size_t i = 0; i < capacity_; ++i) {
				items_[i].~T();
			}
		}
		trace_storage::deallocate(items_);
		items_ = nullptr;
		capacity_ = 0;
		head_ = 0;
		count_ = 0;
		allocation_ = trace_storage::AllocationInfo();
	}

	bool pushDropOldest(const T &item) {
		if (capacity_ == 0 || items_ == nullptr) {
			return false;
		}
		if (count_ < capacity_) {
			items_[physicalIndex(count_)] = item;
			count_++;
			return true;
		}
		items_[head_] = item;
		head_ = (head_ + 1) % capacity_;
		return true;
	}

	bool pushDropNewest(const T &item) {
		if (full()) {
			return false;
		}
		return pushDropOldest(item);
	}

	bool pop(T &out) {
		if (empty()) {
			return false;
		}
		out = items_[head_];
		head_ = (head_ + 1) % capacity_;
		count_--;
		if (count_ == 0) {
			head_ = 0;
		}
		return true;
	}

	bool peek(size_t index, T &out) const {
		if (index >= count_ || items_ == nullptr) {
			return false;
		}
		out = items_[physicalIndex(index)];
		return true;
	}

	void clear() {
		head_ = 0;
		count_ = 0;
	}

	size_t size() const {
		return count_;
	}

	size_t capacity() const {
		return capacity_;
	}

	bool empty() const {
		return count_ == 0;
	}

	bool full() const {
		return capacity_ > 0 && count_ >= capacity_;
	}

	size_t allocatedBytes() const {
		return allocation_.bytes;
	}

	bool usingPsram() const {
		return allocation_.psram;
	}

  private:
	size_t physicalIndex(size_t logicalIndex) const {
		return (head_ + logicalIndex) % capacity_;
	}

	T *items_ = nullptr;
	size_t capacity_ = 0;
	size_t head_ = 0;
	size_t count_ = 0;
	trace_storage::AllocationInfo allocation_;

	void clearStorage() {
		for (size_t i = 0; i < capacity_; ++i) {
			::new (static_cast<void *>(&items_[i])) T();
		}
	}
};

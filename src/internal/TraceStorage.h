#pragma once

#include "../Trace.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

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

template <typename T>
class TraceRingBuffer {
  public:
	TraceRingBuffer() = default;
	~TraceRingBuffer() {
		deinit();
	}

	TraceRingBuffer(const TraceRingBuffer &) = delete;
	TraceRingBuffer &operator=(const TraceRingBuffer &) = delete;

	bool init(size_t capacity, Strata::Placement placement) {
		static_assert(std::is_trivially_copyable_v<T>, "TraceRingBuffer requires trivial items");
		static_assert(
		    std::is_trivially_destructible_v<T>,
		    "TraceRingBuffer requires trivially destructible items"
		);
		deinit();
		if (capacity == 0) {
			return true;
		}
		if (capacity > std::numeric_limits<size_t>::max() / sizeof(T)) {
			return false;
		}

		Strata::Buffer storage(sizeof(T) * capacity, placement);
		if (storage.data() == nullptr) {
			return false;
		}

		storage_ = std::move(storage);
		items_ = storage_.data<T>();
		capacity_ = capacity;
		clearStorage();
		return true;
	}

	void deinit() {
		if (items_ != nullptr) {
			for (size_t i = 0; i < capacity_; ++i) {
				std::destroy_at(&items_[i]);
			}
		}
		storage_.reset();
		items_ = nullptr;
		capacity_ = 0;
		head_ = 0;
		count_ = 0;
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
		return storage_.size();
	}

	Strata::Placement placement() const {
		return storage_.placement();
	}

	Strata::Region region() const {
		return storage_.region();
	}

  private:
	size_t physicalIndex(size_t logicalIndex) const {
		return (head_ + logicalIndex) % capacity_;
	}

	Strata::Buffer storage_;
	T *items_ = nullptr;
	size_t capacity_ = 0;
	size_t head_ = 0;
	size_t count_ = 0;

	void clearStorage() {
		for (size_t i = 0; i < capacity_; ++i) {
			std::construct_at(&items_[i]);
		}
	}
};

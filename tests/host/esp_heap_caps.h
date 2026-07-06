#pragma once

#include <cstddef>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2

inline size_t heap_caps_get_total_size(int caps) {
	(void)caps;
	return 0;
}

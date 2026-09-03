#pragma once

#include <cstdint>
#include <mutex>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using StackType_t = uint32_t;
using configSTACK_DEPTH_TYPE = uint32_t;

struct StaticTask_t {
	uint8_t reserved = 0;
};

struct StaticSemaphore_t {
	alignas(std::recursive_mutex) unsigned char storage[sizeof(std::recursive_mutex)]{};
	bool constructed = false;
};

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr BaseType_t tskNO_AFFINITY = -1;
constexpr UBaseType_t tskIDLE_PRIORITY = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define configSUPPORT_STATIC_ALLOCATION 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

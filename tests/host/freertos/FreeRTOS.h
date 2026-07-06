#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using StackType_t = uint32_t;
using configSTACK_DEPTH_TYPE = uint32_t;

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr BaseType_t tskNO_AFFINITY = -1;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define configSUPPORT_STATIC_ALLOCATION 0
#define INCLUDE_uxTaskGetStackHighWaterMark 1

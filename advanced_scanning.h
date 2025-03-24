#pragma once

#include <windows.h>
#include "settings.h"

typedef enum {
    VALUE_TYPE_INT,	// 4 bytes
    VALUE_TYPE_FLOAT, // 4 bytes
    VALUE_TYPE_DOUBLE, // 8 bytes
    VALUE_TYPE_SHORT, // 2 bytes
    VALUE_TYPE_BYTE, // 1 byte
    VALUE_TYPE_AUTO // Auto-detect
} ValueType;

// SIMD-accelerated integer value search
bool ScanForIntValueSIMD(const BYTE* buffer, size_t bufferSize, int valueToFind);
bool ScanForFloatValueSIMD(const BYTE* buffer, size_t bufferSize, float valueToFind);

// Smart auto-detection of value type
ValueType DetectValueType(const BYTE* buffer, size_t bufferSize, int valueToFind);

// Smart scan implementation that determines best strategy based on value and settings
bool SmartScan(const BYTE* buffer, size_t bufferSize, int valueToFind, 
               ValueType type, const Settings* settings, DWORD* outAddress = nullptr);

// SSE helper function
__m128 _mm_abs_ps(__m128 x);

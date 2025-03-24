#include <windows.h>
#include <immintrin.h>
#include <cmath>
#include <vector>
#include <memory>
#include "logging.h"
#include "settings.h"

typedef enum {
    VALUE_TYPE_INT,
    VALUE_TYPE_FLOAT,
    VALUE_TYPE_DOUBLE,
    VALUE_TYPE_SHORT,
    VALUE_TYPE_BYTE,
    VALUE_TYPE_AUTO
} ValueType;


inline __m128 _mm_abs_ps(__m128 x) {
    static const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));
    return _mm_andnot_ps(sign_mask, x);
}

bool ScanForIntValueSIMD(const BYTE* buffer, size_t bufferSize, int valueToFind) {
    if (bufferSize < 16) {
        for (size_t i = 0; i <= bufferSize - sizeof(int); i += sizeof(int)) {
            int value;
            memcpy(&value, &buffer[i], sizeof(int));
            if (value == valueToFind)
                return true;
        }
        return false;
    }
    
    __m128i searchValue = _mm_set1_epi32(valueToFind);
    
    size_t simdLimit = bufferSize - (bufferSize % 16);
    for (size_t i = 0; i < simdLimit; i += 16) {
        __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&buffer[i]));
        __m128i cmp = _mm_cmpeq_epi32(data, searchValue);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            return true;
        }
    }
	
    for (size_t i = simdLimit; i <= bufferSize - sizeof(int); i += sizeof(int)) {
        int value;
        memcpy(&value, &buffer[i], sizeof(int));
        if (value == valueToFind)
            return true;
    }
    
    return false;
}

bool ScanForFloatValueSIMD(const BYTE* buffer, size_t bufferSize, float valueToFind) {
    if (bufferSize < 16) {
        for (size_t i = 0; i <= bufferSize - sizeof(float); i += sizeof(float)) {
            float value;
            memcpy(&value, &buffer[i], sizeof(float));
            if (!std::isnan(value) && !std::isinf(value) && 
                std::abs(value - valueToFind) < 0.0001f)
                return true;
        }
        return false;
    }
    
    __m128 searchValue = _mm_set1_ps(valueToFind);
    
    size_t simdLimit = bufferSize - (bufferSize % 16);
    for (size_t i = 0; i < simdLimit; i += 16) {
        __m128 data = _mm_loadu_ps(reinterpret_cast<const float*>(&buffer[i]));
        __m128 diff = _mm_sub_ps(data, searchValue);
        __m128 absDiff = _mm_abs_ps(diff);
        __m128 epsilon = _mm_set1_ps(0.0001f);
        __m128 cmp = _mm_cmplt_ps(absDiff, epsilon);
        
        int mask = _mm_movemask_ps(cmp);
        if (mask != 0) {
            for (int j = 0; j < 4; j++) {
                if (mask & (1 << j)) {
                    float value;
                    memcpy(&value, &buffer[i + j * sizeof(float)], sizeof(float));
                    if (!std::isnan(value) && !std::isinf(value))
                        return true;
                }
            }
        }
    }
    
    for (size_t i = simdLimit; i <= bufferSize - sizeof(float); i += sizeof(float)) {
        float value;
        memcpy(&value, &buffer[i], sizeof(float));
        if (!std::isnan(value) && !std::isinf(value) && 
            std::abs(value - valueToFind) < 0.0001f)
            return true;
    }
    
    return false;
}

ValueType DetectValueType(const BYTE* buffer, size_t bufferSize, int valueToFind) {
    if (bufferSize < 8) return VALUE_TYPE_INT;
    if (valueToFind >= 0 && valueToFind <= 255) {
        int countAsInt = 0;
        int countAsByte = 0;
        
        for (size_t i = 0; i < std::min(bufferSize, size_t(1024)); i++) {
            if (buffer[i] == (BYTE)valueToFind) {
                countAsByte++;
            }
            if (i <= bufferSize - sizeof(int)) {
                int value;
                memcpy(&value, &buffer[i], sizeof(int));
                if (value == valueToFind) {
                    countAsInt++;
                }
            }
        }
        
        if (countAsByte > countAsInt * 3) {
            LOG_DEBUG("Auto-detected value %d as BYTE type (byte count: %d, int count: %d)", 
                      valueToFind, countAsByte, countAsInt);
            return VALUE_TYPE_BYTE;
        }
    }
    
    float floatValue = (float)valueToFind;
    if (floatValue >= 0.1f && floatValue <= 10000.0f) {
        int countAsInt = 0;
        int countAsFloat = 0;
        
        for (size_t i = 0; i <= bufferSize - sizeof(float); i += sizeof(float)) {
            float value;
            memcpy(&value, &buffer[i], sizeof(float));
            if (!std::isnan(value) && !std::isinf(value) && 
                std::abs(value - floatValue) < 0.0001f) {
                countAsFloat++;
            }
            
            int intValue;
            memcpy(&intValue, &buffer[i], sizeof(int));
            if (intValue == valueToFind) {
                countAsInt++;
            }
        }
        
        if (countAsFloat > countAsInt) {
            LOG_DEBUG("Auto-detected value %d as FLOAT type (float count: %d, int count: %d)", 
                      valueToFind, countAsFloat, countAsInt);
            return VALUE_TYPE_FLOAT;
        }
    }
    
    return VALUE_TYPE_INT;
}

bool SmartScan(const BYTE* buffer, size_t bufferSize, int valueToFind, 
               ValueType type, const Settings* settings, DWORD* outAddress) {
    ValueType effectiveType = type;
    if (type == VALUE_TYPE_AUTO) {
        effectiveType = DetectValueType(buffer, bufferSize, valueToFind);
    }
    
    bool found = false;
    size_t foundOffset = 0;
    
    if (settings->useVectorizedOperations) {
        switch (effectiveType) {
            case VALUE_TYPE_INT:
                found = ScanForIntValueSIMD(buffer, bufferSize, valueToFind);
                break;
            case VALUE_TYPE_FLOAT:
                found = ScanForFloatValueSIMD(buffer, bufferSize, (float)valueToFind);
                break;
            default:
                for (size_t i = 0; i <= bufferSize - sizeof(int); i++) {
                    int value;
                    memcpy(&value, &buffer[i], sizeof(value));
                    if (value == valueToFind) {
                        found = true;
                        foundOffset = i;
                        break;
                    }
                }
                break;
        }
    } else {
        int typeSize = 4;
        switch (effectiveType) {
            case VALUE_TYPE_BYTE: typeSize = 1; break;
            case VALUE_TYPE_SHORT: typeSize = 2; break;
            case VALUE_TYPE_INT: 
            case VALUE_TYPE_FLOAT: typeSize = 4; break;
            case VALUE_TYPE_DOUBLE: typeSize = 8; break;
            default: typeSize = 4; break;
        }
        
        for (size_t i = 0; i <= bufferSize - typeSize; i++) {
            bool matches = false;
            
            switch (effectiveType) {
                case VALUE_TYPE_INT: {
                    int value;
                    memcpy(&value, &buffer[i], sizeof(value));
                    matches = (value == valueToFind);
                    break;
                }
                case VALUE_TYPE_FLOAT: {
                    float value;
                    memcpy(&value, &buffer[i], sizeof(value));
                    matches = (!std::isnan(value) && !std::isinf(value) && 
                               std::abs(value - (float)valueToFind) < 0.0001f);
                    break;
                }
                case VALUE_TYPE_SHORT: {
                    short value;
                    memcpy(&value, &buffer[i], sizeof(value));
                    matches = (value == (short)valueToFind);
                    break;
                }
                case VALUE_TYPE_BYTE: {
                    BYTE value = buffer[i];
                    matches = (value == (BYTE)valueToFind);
                    break;
                }
                default:
                    break;
            }
            
            if (matches) {
                found = true;
                foundOffset = i;
                break;
            }
        }
    }
    
    if (found && outAddress) {
        *outAddress = (DWORD)foundOffset;
    }
    
    return found;
}

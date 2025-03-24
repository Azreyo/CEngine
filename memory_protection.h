#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MemoryProtectionContext;

#define MEMORY_PROTECT_READ     (1 << 0)
#define MEMORY_PROTECT_WRITE    (1 << 1)
#define MEMORY_PROTECT_EXECUTE  (1 << 2)

MemoryProtectionContext* CreateProtectionContext(HANDLE processHandle, LPVOID address, SIZE_T size);
bool ModifyMemoryProtection(MemoryProtectionContext* context, DWORD desiredProtection);
bool RestoreMemoryProtection(MemoryProtectionContext* context);
void DestroyProtectionContext(MemoryProtectionContext* context);
bool IsMemoryProtected(HANDLE processHandle, LPVOID address);
DWORD GetCurrentProtection(HANDLE processHandle, LPVOID address);
MemoryProtectionContext* EnsureMemoryAccessWithContext(HANDLE processHandle, LPVOID address, SIZE_T size, DWORD requiredAccess);
const char* GetLastErrorAsString(DWORD errorCode);
bool SafeReadMemoryWithRetry(HANDLE processHandle, LPVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead, int maxRetries = 3);

#ifdef __cplusplus
}
#endif

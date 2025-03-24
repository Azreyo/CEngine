#include <windows.h>
#include "logging.h"
#include "memory_protection.h"

struct MemoryProtectionContext {
    HANDLE processHandle; // Process handle
    LPVOID address; // Memory address
    SIZE_T size; // Size of memory region
    DWORD oldProtection; // Store previous protection
    bool protectionChanged; // Track if protection was changed
    bool isTemporary; // Track if protection change is temporary
    bool wasGuardPage; // Track if page was originally guarded
    DWORD originalProtection; // Store complete original protection
};

extern "C" {

MemoryProtectionContext* CreateProtectionContext(HANDLE processHandle, LPVOID address, SIZE_T size) {
    if (!processHandle || !address || size == 0) {
        LOG_ERROR("Invalid parameters for protection context");
        return nullptr;
    }
    
    MemoryProtectionContext* context = new MemoryProtectionContext();
    if (!context) {
        LOG_ERROR("Failed to allocate protection context");
        return nullptr;
    }
    
    context->processHandle = processHandle;
    context->address = address;
    context->size = size;
    context->oldProtection = 0;
    context->protectionChanged = false;
    context->isTemporary = false;
    context->wasGuardPage = false;
    context->originalProtection = 0;
    
    return context;
}

bool ModifyMemoryProtection(MemoryProtectionContext* context, DWORD desiredProtection) {
    if (!context || !context->processHandle) {
        LOG_ERROR("Invalid memory protection context");
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(context->processHandle, context->address, &mbi, sizeof(mbi))) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to query memory at 0x%p (Error: %lu - %s)", 
                 context->address, error, GetLastErrorAsString(error));
        return false;
    }

    LOG_DEBUG("Memory protection at 0x%p - Current: 0x%X, State: 0x%X, Type: 0x%X", 
             context->address, mbi.Protect, mbi.State, mbi.Type);

    context->originalProtection = mbi.Protect;
    context->wasGuardPage = (mbi.Protect & PAGE_GUARD) != 0;

    if (mbi.State != MEM_COMMIT) {
        LOG_ERROR("Cannot modify protection of non-committed memory at 0x%p (State: 0x%X)", 
                context->address, mbi.State);
        return false;
    }

    if ((mbi.Protect & desiredProtection) == desiredProtection) {
        context->protectionChanged = false;
        LOG_DEBUG("Memory at 0x%p already has required protection 0x%X", 
                context->address, desiredProtection);
        return true;
    }

    DWORD newProtection = desiredProtection;
    if (context->wasGuardPage) {
        newProtection |= PAGE_GUARD;
    }

    DWORD oldProtect = 0;
    if (VirtualProtectEx(context->processHandle, context->address, 
                        context->size, newProtection, &context->oldProtection)) {
        context->protectionChanged = true;
        LOG_DEBUG("Successfully changed memory protection at 0x%p from 0x%X to 0x%X", 
                 context->address, context->oldProtection, newProtection);
        return true;
    }

    DWORD error = GetLastError();
    LOG_ERROR("Failed to change memory protection at 0x%p (Error: %lu - %s)", 
             context->address, error, GetLastErrorAsString(error));
    
    if (error == ERROR_INVALID_PARAMETER) {
        DWORD alternativeProtection = PAGE_READWRITE;
        LOG_DEBUG("Trying alternative protection 0x%X for address 0x%p", 
                 alternativeProtection, context->address);
        
        if (VirtualProtectEx(context->processHandle, context->address, 
                           context->size, alternativeProtection, &context->oldProtection)) {
            context->protectionChanged = true;
            LOG_DEBUG("Successfully used alternative protection 0x%X for address 0x%p", 
                     alternativeProtection, context->address);
            return true;
        }
    }
    
    return false;
}

bool RestoreMemoryProtection(MemoryProtectionContext* context) {
    if (!context || !context->processHandle || !context->protectionChanged) {
        return true;
    }

    DWORD tempOldProtection;
    if (VirtualProtectEx(context->processHandle, context->address,
                        context->size, context->oldProtection, &tempOldProtection)) {
        LOG_DEBUG("Restored memory protection at 0x%p to 0x%X", 
                 context->address, context->oldProtection);
        context->protectionChanged = false;
        return true;
    }

    LOG_ERROR("Failed to restore memory protection at 0x%p (Error: %lu)", 
             context->address, GetLastError());
    return false;
}

void DestroyProtectionContext(MemoryProtectionContext* context) {
    if (context) {
        RestoreMemoryProtection(context);
        delete context;
    }
}

MemoryProtectionContext* EnsureMemoryAccessWithContext(HANDLE processHandle, LPVOID address, SIZE_T size, DWORD requiredAccess) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi))) {
        LOG_ERROR("Failed to query memory region");
        return nullptr;
    }

    if ((mbi.Protect & requiredAccess) == requiredAccess) {
        return nullptr;
    }

    MemoryProtectionContext* context = CreateProtectionContext(processHandle, address, size);
    if (!context) {
        return nullptr;
    }

    bool result = ModifyMemoryProtection(context, requiredAccess);
    
    if (result) {
        context->isTemporary = true;
        return context;
    } else {
        DestroyProtectionContext(context);
        return nullptr;
    }
}

bool IsMemoryProtected(HANDLE processHandle, LPVOID address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi))) {
        return true;
    }
    return (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD);
}

DWORD GetCurrentProtection(HANDLE processHandle, LPVOID address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi))) {
        return 0;
    }
    return mbi.Protect;
}

const char* GetLastErrorAsString(DWORD errorCode) {
    static char errorBuffer[256];
    
    if (errorCode == 0) {
        return "No error";
    }
    
    DWORD result = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        errorBuffer,
        sizeof(errorBuffer),
        NULL);
    
    if (result == 0) {
        snprintf(errorBuffer, sizeof(errorBuffer), "Unknown error code: %lu", errorCode);
    } else {
        for (size_t i = result - 1; i > 0; i--) {
            if (errorBuffer[i] == '\r' || errorBuffer[i] == '\n') {
                errorBuffer[i] = '\0';
            } else {
                break;
            }
        }
    }
    
    return errorBuffer;
}

bool SafeReadMemoryWithRetry(HANDLE processHandle, LPVOID address, LPVOID buffer, 
                          SIZE_T size, SIZE_T* bytesRead, int maxRetries) {
    if (maxRetries <= 0) maxRetries = 3;
    
    *bytesRead = 0;
    
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi))) {
        DWORD error = GetLastError();
        LOG_DEBUG("Cannot query memory at 0x%p (Error: %lu - %s)", 
                 address, error, GetLastErrorAsString(error));
        return false;
    }
    
    if (mbi.State != MEM_COMMIT || 
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
        LOG_DEBUG("Memory at 0x%p is not readable (State: 0x%X, Protect: 0x%X)", 
                 address, mbi.State, mbi.Protect);
        return false;
    }
    
    SIZE_T bytesReadThisTime = 0;
    for (int attempt = 0; attempt < maxRetries; attempt++) {
        if (attempt > 0) {
            LOG_DEBUG("Retry %d reading memory at 0x%p", attempt, address);
        }
        
        BOOL result = ReadProcessMemory(
            processHandle,
            address,
            buffer,
            size,
            &bytesReadThisTime
        );
        
        if (result && bytesReadThisTime == size) {
            *bytesRead = bytesReadThisTime;
            return true;
        }
        
        if (result && bytesReadThisTime > 0 && bytesReadThisTime < size) {
            LOG_DEBUG("Partial read at 0x%p: %zu of %zu bytes", 
                     address, bytesReadThisTime, size);
            
            *bytesRead = bytesReadThisTime;
            
            SIZE_T remaining = size - bytesReadThisTime;
            BYTE* bufferPos = (BYTE*)buffer + bytesReadThisTime;
            BYTE* addressPos = (BYTE*)address + bytesReadThisTime;
            
            SIZE_T additionalBytesRead = 0;
            if (ReadProcessMemory(processHandle, addressPos, bufferPos, 
                                remaining, &additionalBytesRead)) {
                *bytesRead += additionalBytesRead;
                if (*bytesRead == size) {
                    return true;
                }
            }
            
            LOG_DEBUG("Could only read %zu of %zu bytes at 0x%p", 
                     *bytesRead, size, address);
            return false;
        }
        
        if (attempt < maxRetries - 1) {
            Sleep(5);
        }
    }
    
    DWORD error = GetLastError();
    LOG_DEBUG("Failed to read memory at 0x%p after %d attempts (Error: %lu - %s)", 
             address, maxRetries, error, GetLastErrorAsString(error));
    return false;
}

}

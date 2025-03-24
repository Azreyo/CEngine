#include "settings.h"
#include "logging.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string.h>
#include "include/imgui.h"
#include <algorithm>
#include <thread>

Settings g_settings;

void initSettings(Settings* settings) {
    if (!settings) return;
    
    ZeroMemory(settings, sizeof(Settings));
    
	// General settings
    settings->threadCount = 4; // Default to 4 threads
    settings->enableLogging = true; // Enable logging 
    settings->enableDeepScan = false; // Disable deep scan
    settings->autoRefreshResults = true; // Auto-refresh results
    settings->refreshInterval = 1000; // 1 second
    settings->alwaysOnTop = false; // Not always on top
    settings->scanBufferMB = 16;  // 16MB buffer
    settings->darkMode = true;   // Dark mode UI
    settings->scanPriority = SCAN_PRIORITY_SPEED; // Speed priority
    settings->showWelcomeGuide = true; // Show welcome guide
    // Security settings
    settings->useBytePatternScanning = false; // Disable pattern scanning
    settings->useFuzzyScanning = false; // Disable fuzzy scanning
    settings->scanUnalignedAddresses = false; // Disable unaligned scanning
    settings->detectPointerChains = false; // Disable pointer chain detection
    settings->maxPointerDepth = 3; // Maximum 3 levels deep
    // Memory protection settings
    settings->overwriteMemoryProtection = false;	
    settings->backupMemoryBeforeWrite = true; // Backup memory before writing
    settings->validateMemoryWrites = true; // Validate memory writes
    settings->preventStackWrites = true; // Prevent writing to stack memory
    settings->preventExecutableWrites = true; // Prevent writing to executable memory
    settings->validateAddressRange = true; // Validate memory access ranges 
    settings->preventExecutableModification = true; // Prevent modification of executable memory 
    settings->detectMemoryTraps = true; // Detect and avoid memory traps 
    settings->maxProtectedRegions = 1000; // Limit to 1000 regions
	// Performance settings
    settings->useMemoryMappedScanning = true; // Enable memory-mapped scanning
    settings->useVectorizedOperations = true;  // Enable SIMD operations
    settings->scanAlignment = 16; // 16-byte alignment
    settings->scanChunkMultiplier = 2; // Double the default chunk size
    settings->adaptiveThreading = true; // Enable adaptive threading
	settings->cacheOptimization = true; // Enable cache optimization
	settings->searchTimeoutMs = 5000;  // 5 seconds timeout
    // UI settings
    settings->showToolbar = true; // Show toolbar
    settings->showStatusBar = true; // Show status bar
    settings->showMemoryMap = false; // Hide memory map by default
    settings->compactMode = false; // Use standard UI mode
    settings->maxRecentFiles = 10; // Maximum of 10 recent files
    settings->showGridLines = true; // Show grid lines
    settings->useCustomColors = false; // Disable custom colors
    settings->highlightColor = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
    settings->warningColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
    settings->showAdvancedOptions = false; // Hide advanced options
    settings->useCompactLayout = false; // Use standard layout
    // Data export settings
    settings->autoSaveResults = false; // Disable auto-save
    strcpy_s(settings->resultsSavePath, sizeof(settings->resultsSavePath), ""); // Empty path
    settings->exportWithTimestamp = true; // Add timestamps to exports
    settings->autoSaveInterval = 5; // 5 minutes

	// New settings
    settings->useMemoryMappedIO = true; // Enable memory-mapped I/O
    settings->usePageAlignedScans = true; // Align scans to page boundaries
    settings->useOptimizedSearch = true;  // Enable optimized search algorithms
    settings->prefetchDistance = 4; // Prefetch distance in pages
    settings->useVectorizedSearch = true; // Enable SIMD search
	// New memory protection settings
    settings->enforceRangeChecks = true; // Enforce strict memory range checking
    settings->preventSystemCrash = true; // Additional crash prevention measures
    settings->logSecurityViolations = true; // Log security violations
    settings->maxRetryAttempts = 3;
	// New optimization settings
    settings->minThreadCount = 2; // Minimum 2 threads
    settings->maxThreadCount = std::thread::hardware_concurrency(); // Maximum hardware threads
    settings->useAdaptiveThreading = true; // Enable adaptive threading
    settings->threadPriorityLevel = 3; // Default to normal priority
    settings->threadStackSize = 1024 * 1024; // 1MB stack size
    settings->pinThreadsToCores = true; // Pin threads to CPU cores
    // New UI customization
    settings->minBufferSize = 1;     // 1MB
    settings->maxBufferSize = 64;    // 64MB
    settings->useBufferPooling = true; // Enable buffer pooling
    settings->bufferPoolSize = 256;  // 256MB pool
    settings->bufferAlignment = 4096; // 4KB alignment
    settings->useLargePages = false; // Disabled by default
    settings->prefetchMemory = true; // Enable memory prefetching
    settings->prefetchDistance = 4; // 4 pages
    // New security features
    settings->useVectorInstructions = true;  // Enable vector instructions
    settings->useParallelProcessing = true; // Enable parallel processing
    settings->scanChunkSize = 1024 * 1024;  // 1MB chunks
    settings->optimizeForSpeed = true; 	// Optimize for speed
    settings->scanBatchSize = 1000;  // 1000 results per batch
    settings->useIntelIPP = false; // Disable Intel IPP
}

const char* getSettingsFilePath() {
    static char filePath[MAX_PATH] = {0};
    
    if (filePath[0] == '\0') {
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {
            LOG_ERROR("Failed to get executable path");
            return "CEngine.settings";
        }

        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash) {
            *(lastSlash + 1) = '\0';
            strcpy_s(filePath, sizeof(filePath), exePath);
            strcat_s(filePath, sizeof(filePath), "CEngine.settings");
        } else {
            strcpy_s(filePath, sizeof(filePath), "CEngine.settings");
        }
    }
    
    return filePath;
}

bool loadSettings(Settings* settings, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        initSettings(settings);
        return false;
    }

    size_t read = fread(settings, sizeof(Settings), 1, file);
    fclose(file);
    
    if (read != 1) {
        initSettings(settings);
        return false;
    }
    
    if (settings->threadCount <= 0 || settings->threadCount > 32) {
        settings->threadCount = 2;
    }
    
    if (settings->refreshInterval < 100 || settings->refreshInterval > 10000) {
        settings->refreshInterval = 1000;
    }
    
    if (settings->scanBufferMB < 1 || settings->scanBufferMB > 1024) {  // Changed limits to be MB appropriate
        settings->scanBufferMB = 16;  // Default to 16MB
    }
    
    return true;
}

bool saveSettings(const Settings* settings, const char* filename) {  // Changed from SaveSettings
    if (!filename) {
        LOG_ERROR("Invalid filename provided");
        return false;
    }
    
    FILE* file = fopen(filename, "wb");
    if (file) {
        size_t written = fwrite(settings, sizeof(Settings), 1, file);
        fclose(file);
        
        if (written == 1) {
            LOG_DEBUG("Settings saved successfully to %s", filename);
            return true;
        } else {
            LOG_ERROR("Failed to write settings data to %s", filename);
        }
    } else {
        LOG_ERROR("Failed to open settings file %s for writing", filename);
    }
    return false;
}

bool loadSettingsOrDefault(Settings* settings) {
    const char* settingsPath = getSettingsFilePath();
    
    if (loadSettings(settings, settingsPath)) {
        LOG_INFO("Loaded saved settings from: %s", settingsPath);
        validateSettings(settings);
        return true;
    }
    
    LOG_INFO("No saved settings found, using defaults");
    initSettings(settings);
    return false;
}

void validateSettings(Settings* settings) {
    // Thread settings validation
    settings->minThreadCount = std::max(1, std::min(settings->minThreadCount, 64));
    settings->maxThreadCount = std::max(settings->minThreadCount, 
                                      std::min(settings->maxThreadCount, 
                                      (int)std::thread::hardware_concurrency()));
    
    // Buffer settings validation
    settings->minBufferSize = std::max(1ULL, std::min(settings->minBufferSize, 1024ULL));
    settings->maxBufferSize = std::max(settings->minBufferSize, 
                                     std::min(settings->maxBufferSize, 4096ULL));
    
    settings->bufferAlignment = std::max(4096, 
                                       std::min(settings->bufferAlignment, 65536));
    
    // Performance settings validation - Fix type mismatch
    settings->scanChunkSize = std::max(size_t(4096), 
                                     std::min(settings->scanChunkSize, 
                                     size_t(16) * 1024 * 1024));
    
    settings->scanBatchSize = std::max(100, std::min(settings->scanBatchSize, 10000));
    
    LOG_DEBUG("Settings validated and adjusted if necessary");
}

bool autoSaveSettings(const Settings* settings) {
    static DWORD lastSaveTime = 0;
    DWORD currentTime = GetTickCount();
    
    if (currentTime - lastSaveTime < 5000) {
        return true;
    }
    
    if (saveSettings(settings, getSettingsFilePath())) {
        lastSaveTime = currentTime;
        LOG_DEBUG("Settings auto-saved successfully");
        return true;
    }
    
    LOG_ERROR("Failed to auto-save settings");
    return false;
}
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/imgui.h"

typedef enum {
    SCAN_PRIORITY_SPEED,   // Scan faster but less thorough
    SCAN_PRIORITY_THOROUGH,  // Scan all regions but slower
    SCAN_PRIORITY_COMPLETE  // Scan all regions with maximum detail
} ScanPriority;

typedef struct {
	// General Settings
    int threadCount;         // Number of threads to use for scanning
    bool enableLogging;      // Enable logging of operations
    bool enableDeepScan;     // Enable deep scanning (more thorough but slower)
    bool autoRefreshResults; // Auto-refresh memory values in results
    int refreshInterval;     // Refresh interval in milliseconds
    bool alwaysOnTop;        // Keep window always on top
    int scanBufferMB;        // Size of scan buffer in MB
    bool darkMode;           // Dark mode UI
    ScanPriority scanPriority; // Scan priority setting
    bool showWelcomeGuide;    // Show welcome guide on first run
    bool requireWriteConfirmation;    // Confirm before writing to memory
    bool allowSystemProcesses;        // Allow access to system processes
    bool automaticPrivilegeElevation; // Auto elevate privileges if needed
    bool overwriteMemoryProtection;   // Allow writing to read-only memory
    bool logSecurityEvents;           // Log security-sensitive operations
    bool enableLogConsole;            // Show log console
    
	// Optimization Settings
    bool cacheOptimization;           // Optimize memory access patterns for cache
    int searchTimeoutMs;              // Timeout for search operations in milliseconds
    
	// Performance Settings
    size_t maxScanResults;    // Maximum number of scan results to store
    bool autoReduceThreads;   // Automatically reduce threads if memory limited
    size_t scanChunkSize;     // Size of memory chunks to scan at once
    int resultBatchSize;     // Number of results to process in one batch
    bool usePageAlignment;   // Align scans to page boundaries
    bool skipZeroRegions;    // Skip regions filled with zeros
    bool skipSystemRegions;  // Skip system DLL regions
    bool enableMultiScan;    // Enable scanning multiple values at once
    int threadPriority;      // Thread priority for scanning

    // Advanced Scanning Options
    bool useBytePatternScanning;    // Enable pattern-based scanning
    bool useFuzzyScanning;          // Allow approximate value matches
    float fuzzyThreshold;           // Threshold for fuzzy matching (0.0-1.0)
    bool scanUnalignedAddresses;    // Allow scanning unaligned memory addresses
    bool detectPointerChains;       // Try to detect pointer chains
    int maxPointerDepth;           // Maximum depth for pointer chain detection
    
    // Memory Protection Options
    bool backupMemoryBeforeWrite;   // Create backup before writing
    bool validateMemoryWrites;      // Verify writes were successful
    bool preventStackWrites;        // Prevent writing to stack memory
    bool preventExecutableWrites;   // Prevent writing to executable memory
    
    // UI Customization
    bool showToolbar;               // Show toolbar with common actions
    bool showStatusBar;             // Show status bar
    bool showMemoryMap;             // Show memory map by default
    bool compactMode;               // Use compact UI mode
    int maxRecentFiles;            // Number of recent files to remember
    bool showGridLines;             // Show grid lines in results
    
    // Data Export Options
    bool autoSaveResults;           // Auto-save scan results
    char resultsSavePath[256];      // Path for auto-saving results
    bool exportWithTimestamp;       // Add timestamps to exports
    int autoSaveInterval;          // Interval for auto-saving (minutes)

    // New settings
    bool useMemoryMappedIO;         // Use memory mapped I/O for scanning
    bool usePageAlignedScans;       // Align scans to page boundaries
    bool useOptimizedSearch;        // Use optimized search algorithms
    int prefetchDistance;           // Memory prefetch distance in pages
    bool useVectorizedSearch;       // Use SIMD/vectorized search when available

    // New Memory Protection Settings
    bool validateAddressRange;      // Validate address ranges before access
    bool preventExecutableModification; // Prevent modification of executable code
    bool detectMemoryTraps;        // Detect and avoid memory traps
    size_t maxProtectedRegions;    // Maximum number of protected regions to track
    
    // New Optimization Settings
    bool useMemoryMappedScanning;  // Use memory mapped scanning when possible
    bool useVectorizedOperations;  // Use CPU vectorization for scanning
    size_t scanAlignment;          // Memory alignment for scanning (4, 8, 16 bytes)
    int scanChunkMultiplier;       // Multiplier for scan chunk size
    bool adaptiveThreading;        // Adjust thread count based on system load
    
    // New UI Customization
    bool useCustomColors;          // Enable custom color scheme
    ImVec4 highlightColor;        // Color for highlighting changes
    ImVec4 warningColor;          // Color for warnings
    bool showAdvancedOptions;      // Show advanced options in UI
    bool useCompactLayout;         // Use compact UI layout
    
    // New Security Features
    bool enforceRangeChecks;       // Enforce strict memory range checking
    bool preventSystemCrash;       // Additional crash prevention measures
    bool logSecurityViolations;    // Log security violations
    int maxRetryAttempts;         // Maximum retry attempts for operations

    // New Thread Management Settings
    int minThreadCount;              // Minimum number of threads to use
    int maxThreadCount;             // Maximum number of threads to use
    bool useAdaptiveThreading;      // Adjust thread count based on CPU load
    int threadPriorityLevel;        // Thread priority (1-5)
    size_t threadStackSize;         // Custom thread stack size
    bool pinThreadsToCores;         // Pin threads to specific CPU cores
    
    // New Memory Buffer Settings
    size_t minBufferSize;           // Minimum buffer size in MB
    size_t maxBufferSize;           // Maximum buffer size in MB
    bool useBufferPooling;          // Enable buffer pooling
    size_t bufferPoolSize;          // Size of buffer pool in MB
    int bufferAlignment;            // Buffer alignment (4096, 8192, etc)
    bool useLargePages;             // Use large pages for buffers
    bool prefetchMemory;            // Enable memory prefetching
    
    // New Performance Settings
    bool useVectorInstructions;     // Use CPU vector instructions
    bool useParallelProcessing;     // Enable parallel processing
    bool optimizeForSpeed;          // Optimize for speed vs memory usage
    int scanBatchSize;             // Number of addresses to process in batch
    bool useIntelIPP;              // Use Intel IPP library if available
    
} Settings;

void initSettings(Settings* settings);
void validateSettings(Settings* settings);
bool loadSettingsOrDefault(Settings* settings);
bool loadSettings(Settings* settings, const char* filename);
bool saveSettings(const Settings* settings, const char* filename);
const char* getSettingsFilePath();

extern Settings g_settings;

#endif
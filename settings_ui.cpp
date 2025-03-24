#include "settings_ui.h"
#include "include/imgui.h"
#include "logging.h"
#include "settings.h"

extern void ShowStatusMessage(const char* message);
extern bool g_firstRun;
extern bool g_resultsUpdated;

extern bool saveSettings(const Settings* settings, const char* filename);

namespace ImGui {
    void HelpMarker(const char* desc);
}

void ImGui::HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool ShowSettingsDialog(bool* p_open, Settings* settings) {
    bool settingsChanged = false;
    
    if (!p_open || !settings) {
        return false;
    }
    
    Settings originalSettings = *settings;
    
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings##dialog", p_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return false;
    }
    
    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Performance##tab")) {
            ImGui::Text("Performance Settings");
            ImGui::Separator();
            
            int threadCount = settings->threadCount;
            if (ImGui::SliderInt("Thread Count##perf", &threadCount, 1, 16)) {
                settings->threadCount = threadCount;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Number of threads to use for scanning");
            
            int bufferSizeMB = settings->scanBufferMB;
            if (ImGui::SliderInt("Scan Buffer Size (MB)##perf", &bufferSizeMB, 1, 1024)) {
                settings->scanBufferMB = bufferSizeMB;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Size of the buffer used for scanning memory in megabytes\n"
                                                 "Larger buffers can scan more memory but may be slower");
            
            bool cacheOptimize = settings->cacheOptimization;
            if (ImGui::Checkbox("Cache Optimization##perf", &cacheOptimize)) {
                settings->cacheOptimization = cacheOptimize;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Optimize memory access patterns for cache efficiency");
            
            int searchTimeout = settings->searchTimeoutMs;
            if (ImGui::SliderInt("Search Timeout (ms)##perf", &searchTimeout, 0, 30000)) {
                settings->searchTimeoutMs = searchTimeout;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Maximum time for a search operation before timing out (0 = no limit)");
            
            ImGui::Separator();
            ImGui::Text("Optimization Settings");
            ImGui::Separator();
            
            bool useMMScanning = settings->useMemoryMappedScanning;
            if (ImGui::Checkbox("Use Memory Mapped Scanning", &useMMScanning)) {
                settings->useMemoryMappedScanning = useMMScanning;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Use memory mapped I/O for faster scanning");
            
            bool useVectorizedOperations = settings->useVectorizedOperations;
            if (ImGui::Checkbox("Use Vectorized Operations", &useVectorizedOperations)) {
                settings->useVectorizedOperations = useVectorizedOperations;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Use SIMD/vectorized operations when available");
            
            int scanBatchSize = settings->scanBatchSize;
            if (ImGui::SliderInt("Result Batch Size", &scanBatchSize, 100, 10000)) {
                settings->scanBatchSize = scanBatchSize;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Number of results to process in one batch");
            
            bool prefetchMemory = settings->prefetchMemory;
            if (ImGui::Checkbox("Enable Memory Prefetching", &prefetchMemory)) {
                settings->prefetchMemory = prefetchMemory;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Enable memory prefetching for better performance");
            ImGui::Separator();

            ImGui::Text("Advanced Scanning Options");
            ImGui::Separator();
            
            bool usePattern = settings->useBytePatternScanning;
            if (ImGui::Checkbox("Pattern-based Scanning", &usePattern)) {
                settings->useBytePatternScanning = usePattern;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Enable pattern-based memory scanning");
            
            bool useFuzzy = settings->useFuzzyScanning;
            if (ImGui::Checkbox("Fuzzy Value Matching", &useFuzzy)) {
                settings->useFuzzyScanning = useFuzzy;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Allow for approximate value matching with tolerance");
            
            if (settings->useFuzzyScanning) {
                float threshold = settings->fuzzyThreshold;
                if (ImGui::SliderFloat("Fuzzy Threshold", &threshold, 0.0f, 1.0f, "%.2f")) {
                    settings->fuzzyThreshold = threshold;
                    settingsChanged = true;
                }
            }
            
            bool detectPointers = settings->detectPointerChains;
            if (ImGui::Checkbox("Detect Pointer Chains", &detectPointers)) {
                settings->detectPointerChains = detectPointers;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Attempt to detect pointer chains automatically");
            
            if (settings->detectPointerChains) {
                int depth = settings->maxPointerDepth;
                if (ImGui::SliderInt("Max Pointer Depth", &depth, 1, 10)) {
                    settings->maxPointerDepth = depth;
                    settingsChanged = true;
                }
            }
            
            bool useOptimizedSearch = settings->useOptimizedSearch;
            if (ImGui::Checkbox("Optimized Value Matching", &useOptimizedSearch)) {
                settings->useOptimizedSearch = useOptimizedSearch;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Use optimized algorithms for value matching");
            
            bool useVectorizedSearch = settings->useVectorizedSearch;
            if (ImGui::Checkbox("Use Vectorized Scanning", &useVectorizedSearch)) {
                settings->useVectorizedSearch = useVectorizedSearch;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Use CPU SIMD instructions for faster scanning");
            
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("UI##tab")) {
            ImGui::Text("UI Settings");
            ImGui::Separator();
            
            bool autoRefresh = settings->autoRefreshResults;
            if (ImGui::Checkbox("Auto-Refresh Results##ui", &autoRefresh)) {
                settings->autoRefreshResults = autoRefresh;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Automatically refresh memory values in results table");
            
            if (settings->autoRefreshResults) {
                int refreshInterval = settings->refreshInterval;
                if (ImGui::SliderInt("Refresh Interval (ms)##ui", &refreshInterval, 100, 5000)) {
                    settings->refreshInterval = refreshInterval;
                    settingsChanged = true;
                }
                ImGui::SameLine(); ImGui::HelpMarker("Time between auto-refresh updates");
            }
            
            bool alwaysOnTop = settings->alwaysOnTop;
            if (ImGui::Checkbox("Always On Top##ui", &alwaysOnTop)) {
                settings->alwaysOnTop = alwaysOnTop;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Keep the window always on top of others");
            
            bool darkMode = settings->darkMode;
            if (ImGui::Checkbox("Dark Mode##ui", &darkMode)) {
                settings->darkMode = darkMode;
                settingsChanged = true;
                
                if (darkMode)
                    ImGui::StyleColorsDark();
                else
                    ImGui::StyleColorsLight();
            }
            ImGui::SameLine(); ImGui::HelpMarker("Use dark color theme");
            
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Security##tab")) {
            ImGui::Text("Security Settings");
            ImGui::Separator();
            
            bool confirmWrites = settings->requireWriteConfirmation;
            if (ImGui::Checkbox("Confirm Memory Writes##sec", &confirmWrites)) {
                settings->requireWriteConfirmation = confirmWrites;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Ask for confirmation before writing to memory");
            
            bool allowSystemProc = settings->allowSystemProcesses;
            if (ImGui::Checkbox("Allow System Process Access##sec", &allowSystemProc)) {
                settings->allowSystemProcesses = allowSystemProc;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Enable access to system processes (can be dangerous)");
            
            bool automaticPrivilegeElevation = settings->automaticPrivilegeElevation;
            if (ImGui::Checkbox("Automatic Privilege Elevation##sec", &automaticPrivilegeElevation)) {
                settings->automaticPrivilegeElevation = automaticPrivilegeElevation;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Automatically elevate privileges when required");
            
            bool overwriteMemoryProtection = settings->overwriteMemoryProtection;
            if (ImGui::Checkbox("Overwrite Memory Protection##sec", &overwriteMemoryProtection)) {
                settings->overwriteMemoryProtection = overwriteMemoryProtection;
                settingsChanged = true;
            }
            
            ImGui::SameLine(); ImGui::HelpMarker("Allows modifying read-only memory regions. Use with caution!");

            ImGui::Separator();
            ImGui::Text("Advanced Protection Settings");
            ImGui::Separator();
            
            bool validateRange = settings->validateAddressRange;
            if (ImGui::Checkbox("Validate Address Ranges", &validateRange)) {
                settings->validateAddressRange = validateRange;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Validate memory address ranges before access");
            
            bool preventExec = settings->preventExecutableModification;
            if (ImGui::Checkbox("Prevent Executable Modification", &preventExec)) {
                settings->preventExecutableModification = preventExec;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Prevent modification of executable code segments");
            ImGui::Separator();
            ImGui::Text("Memory Protection Settings");
            ImGui::Separator();
            
            bool backup = settings->backupMemoryBeforeWrite;
            if (ImGui::Checkbox("Backup Before Write", &backup)) {
                settings->backupMemoryBeforeWrite = backup;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Create backup before writing to memory");
            
            bool validate = settings->validateMemoryWrites;
            if (ImGui::Checkbox("Validate Memory Writes", &validate)) {
                settings->validateMemoryWrites = validate;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Verify memory writes were successful");
            
            bool preventStack = settings->preventStackWrites;
            if (ImGui::Checkbox("Prevent Stack Writes", &preventStack)) {
                settings->preventStackWrites = preventStack;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Prevent writing to stack memory regions");
            
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Support##tab")) {
            ImGui::Text("Support Settings & Help");
            ImGui::Separator();
            
            bool enableLogging = settings->enableLogging;
            if (ImGui::Checkbox("Enable Logging##support", &enableLogging)) {
                settings->enableLogging = enableLogging;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Create log files for troubleshooting");
            
            bool enableLogConsole = settings->enableLogConsole;
            if (ImGui::Checkbox("Show Log Console##support", &enableLogConsole)) {
                settings->enableLogConsole = enableLogConsole;
                settingsChanged = true;
            }
            ImGui::SameLine(); ImGui::HelpMarker("Display the log console window");
            
            ImGui::Separator();
            ImGui::Text("About CEngine Memory Scanner");
            ImGui::TextWrapped("A memory analysis and modification tool for process debugging and game hacking education.");
            
            ImGui::Separator();
            if (ImGui::Button("Show Welcome Guide Now##support", ImVec2(180, 30))) {
                settings->showWelcomeGuide = true;
                g_firstRun = true;
                settingsChanged = true;
            }
            
            ImGui::SameLine(); ImGui::HelpMarker("Open the welcome guide immediately");
            
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("Save Settings##settingsbtn", ImVec2(150, 0))) {
        if (saveSettings(settings, getSettingsFilePath())) {
            ShowStatusMessage("Settings saved successfully");
            *p_open = false;
        } else {
            ShowStatusMessage("Failed to save settings");
        }
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Reset to Defaults##settingsbtn", ImVec2(150, 0))) {
        initSettings(settings);
        settingsChanged = true;
        ShowStatusMessage("Settings reset to defaults");
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Cancel##settingsbtn", ImVec2(80, 0))) {
        *settings = originalSettings;
        *p_open = false;
    }
    
    ImGui::End();
    
    return settingsChanged;
}
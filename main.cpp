#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <TlHelp32.h>
#include <algorithm> 
#include <psapi.h>
#include <memory> 
#include <functional> 
#include <cmath>
#include <limits>
#include <stdexcept>
#include <process.h>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <d3d11.h>
#include <immintrin.h>

#include "settings.h"
#include "settings_ui.h"
#include "include/imgui.h"
#include "include/imgui_impl_win32.h"
#include "include/imgui_impl_dx11.h"
#include "logging.h"
#include "log_console.h"
#include "debug_info.h"
#include "memory_protection.h"

#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
bool g_firstRun = true;              // First run state
bool g_vsync = true;                 // VSync flag
bool g_optimizeScanning = true;      // Optimize scanning flag
bool g_showWelcomeGuide = false;     // Show welcome guide
bool g_maxResultsWarningShown = false; // Max results warning flag

char g_statusMessage[256] = "";      // Status message buffer
float g_statusMessageTime = 0.0f;    // Status message display time

void CaptureDebugInfo(const char* error) {
    if (!error) {
        return;
    }
    
    strncpy_s(g_debugInfo.lastError, sizeof(g_debugInfo.lastError), error, _TRUNCATE);
    g_debugInfo.lastError[sizeof(g_debugInfo.lastError) - 1] = '\0';
    
    if (ImGui::GetCurrentContext() == nullptr) {
        g_debugInfo.frameCount = 0;
        g_debugInfo.context = nullptr;
    } else {
        g_debugInfo.frameCount = ImGui::GetFrameCount();
        g_debugInfo.context = ImGui::GetCurrentContext();
    }
    
    g_debugInfo.isWelcomeGuideOpen = g_settings.showWelcomeGuide;
    g_debugInfo.isSettingsOpen = false;
    
    int result = snprintf(g_debugInfo.uiState, sizeof(g_debugInfo.uiState),
        "Frame: %d, WelcomeGuide: %d, FirstRun: %d, Context: %p",
        g_debugInfo.frameCount, g_debugInfo.isWelcomeGuideOpen,
        g_firstRun, g_debugInfo.context);
        
    if (result < 0 || static_cast<size_t>(result) >= sizeof(g_debugInfo.uiState)) {
        g_debugInfo.uiState[sizeof(g_debugInfo.uiState) - 1] = '\0';
        LOG_WARNING("Debug info string truncated");
    }
}


typedef struct {
    DWORD processId;
    HANDLE processHandle;
    char processName[MAX_PATH];
    Settings* settings;
} ProcessInfo;

typedef struct {
    DWORD address;
    int value;
    int originalValue;
} MemoryEntry;

typedef struct {
    MemoryEntry* entries;
    size_t count;
    size_t capacity;
} ScanResults;

ScanResults g_scanResults = {nullptr, 0, 0};

ProcessInfo g_currentProcess = {
    0,                  // processId
    NULL,              // processHandle
    "",                // processName
    &g_settings        // settings pointer
};

typedef struct {
    ProcessInfo* process;
    int valueToFind;
    ScanResults* results;
    Settings* settings;
    std::vector<MEMORY_BASIC_INFORMATION> regions;
} ScanThreadData;

typedef enum {
    VALUE_TYPE_INT,
    VALUE_TYPE_FLOAT,
    VALUE_TYPE_DOUBLE,
    VALUE_TYPE_SHORT,
    VALUE_TYPE_BYTE,
    VALUE_TYPE_AUTO
} ValueType;


ValueType currentValueType = VALUE_TYPE_INT;

const char* valueTypeNames[] = {
    "Integer (4 bytes)",
    "Float (4 bytes)",
    "Double (8 bytes)",
    "Short (2 bytes)",
    "Byte (1 byte)",
    "Auto-detect"
};

bool showProcessList = false;
bool showMemoryRegions = false;
bool showProcessDetails = false;
bool showScanStats = false;
int valueToFind = 0;
char searchBuffer[256] = "";
char g_addressInput[16] = "";

const SIZE_T CHUNK_SIZE = 4096;
const DWORD MAX_THREAD_RUNTIME = 60000;
const DWORD WATCHDOG_CHECK_INTERVAL = 10000;
const DWORD GRACE_PERIOD = 15000;
const SIZE_T SCAN_CHUNK_SIZE = 1024 * 1024;
const DWORD SCAN_BATCH_SIZE = 1000;
const DWORD THREAD_TIMEOUT = 15000;
const DWORD PROGRESS_UPDATE_INTERVAL = 100; 
const size_t INITIAL_RESULTS_CAPACITY = 1024;


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool loadSettingsOrDefault(Settings* settings);
const char* getSettingsFilePath();
bool saveSettings(const Settings* settings, const char* filename);

void listProcesses(ImGuiTableFlags flags);
bool attachToProcess(ProcessInfo* process, DWORD processId);
void scanMemory(ProcessInfo* process, int valueToFind);
void updateMemoryValue(ProcessInfo* process, DWORD address, int newValue);
void displayMemoryRegions(ProcessInfo* process, ImGuiTableFlags flags);
void DisplayScanResults(ImGuiTableFlags flags);
void initScanResults(ScanResults* results);
void optimizeScanResults(ScanResults* results);
bool addScanResult(ScanResults* results, DWORD address, int value);
void freeScanResults(ScanResults* results);
void narrowResults(ProcessInfo* process, ScanResults* results, int newValue);
void ShowStatusMessage(const char* message);
void ShowFormattedStatusMessage(const char* format, ...);
void UpdateResultsDisplay();
int GetValueTypeSize(ValueType type);
bool ValueMatches(const BYTE* buffer, int valueToFind, ValueType type);
void SaveBatchResults(ScanResults* results, std::vector<std::pair<DWORD, int>>& batch);
unsigned __stdcall scanMemoryThreadFunc(void* arg);
void ShowWelcomeGuide();
void ShowScanProgressDialog();

template<typename T>
T min_val(T a, T b) {
    return (a < b) ? a : b;
}

SIZE_T min_val(SIZE_T a, DWORD b) {
    return (a < static_cast<SIZE_T>(b)) ? a : static_cast<SIZE_T>(b);
}

int newValue = 0;
DWORD addressToModify = 0; //
std::atomic<bool> g_cancelScan{false};
double g_scanProgress = 0.0;
size_t g_totalRegionsToScan = 0;
std::atomic<bool> g_threadSignal{true};
std::atomic<bool> g_scanInProgress{false};
std::atomic<size_t> g_regionsScanned{0};
std::atomic<size_t> g_matchesFound{0};
std::atomic<size_t> g_bytesScanned{0};
std::atomic<size_t> g_regionsSkipped{0};
std::atomic<bool> g_resultsUpdated{false};
std::atomic<size_t> g_totalMemoryToScan{0};
float g_scanSpeed = 0.0f;

LogConsole g_logConsole;
std::mutex scanResultsMutex;

ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
IDXGISwapChain* g_pSwapChain = NULL;
ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    if (!loadSettingsOrDefault(&g_settings)) {
        LOG_WARNING("Using default settings - no saved settings found");
    }
    
    initScanResults(&g_scanResults);
    g_currentProcess.settings = &g_settings;

    WNDCLASSEX wc = { 
        sizeof(WNDCLASSEX), 
        CS_CLASSDC, 
        WndProc, 
        0L, 
        0L, 
        GetModuleHandle(NULL), 
        NULL, 
        NULL, 
        NULL, 
        NULL, 
        L"CEngine Memory Scanner", 
        NULL 
    };
    ::RegisterClassEx(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = ::CreateWindow(
        wc.lpszClassName, 
        L"CEngine Memory Scanner", 
        WS_OVERLAPPEDWINDOW, 
        0, 
        0, 
        screenWidth, 
        screenHeight, 
        NULL, 
        NULL, 
        wc.hInstance, 
        NULL
    );

    // Maximize the window
    ShowWindow(hwnd, SW_MAXIMIZE);
    UpdateWindow(hwnd);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowPadding = ImVec2(2.0f, 2.0f);
    style.FramePadding = ImVec2(4.0f, 3.0f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    
    bool showSettingsDialog = false;

    Logger::getInstance().init(g_settings.enableLogging, true);
    LOG_INFO("CEngine started");

    bool done = false;
    static bool firstRun = true;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;


        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("CEngine Memory Scanner", NULL, 
            ImGuiWindowFlags_MenuBar | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoCollapse | 
            ImGuiWindowFlags_NoBringToFrontOnFocus | 
            ImGuiWindowFlags_NoTitleBar);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    done = true;
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Options")) {
                ImGui::MenuItem("VSync", NULL, &g_vsync);
                ImGui::MenuItem("Optimize Scanning", NULL, &g_optimizeScanning);
                
                if (ImGui::MenuItem("Settings...")) {
                    showSettingsDialog = true;
                }
                
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("View")) {
                bool visible = g_logConsole.getVisible();
                if (ImGui::MenuItem("Log Console##view", nullptr, &visible)) {
                    g_logConsole.setVisible(visible);
                }
                if (ImGui::MenuItem("Memory Map##view", nullptr, &showMemoryRegions)) {}
                if (ImGui::MenuItem("Process Details##view", nullptr, &showProcessDetails)) {}
                if (ImGui::MenuItem("Scan Statistics##view", nullptr, &showScanStats)) {}
                
                ImGui::Separator();
                
                ImGui::MenuItem("Always On Top##view", nullptr, &g_settings.alwaysOnTop);
                ImGui::MenuItem("Dark Mode##view", nullptr, &g_settings.darkMode);
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Welcome Guide##helpmenu", nullptr, nullptr)) {
                    g_settings.showWelcomeGuide = true;
                    g_firstRun = true;
                    
                    LOG_INFO("User opened Welcome Guide from Help menu");
                    
                    saveSettings(&g_settings, getSettingsFilePath());
                }
                
                ImGui::Separator();
                

                if (ImGui::MenuItem("About CEngine##help", nullptr, nullptr)) {
                    LOG_INFO("User opened About dialog");
                }
                
                if (ImGui::MenuItem("Documentation##help", nullptr, nullptr)) {

                    LOG_INFO("User requested documentation");
                    ShowStatusMessage("Documentation will open in browser when available");
                }
                
                ImGui::EndMenu();
            }
            
            ImGui::EndMenuBar();
        }
        
        ImGui::Text("Current Process: %s (PID: %lu)", 
            g_currentProcess.processName[0] ? g_currentProcess.processName : "None", 
            g_currentProcess.processId);
        ImGui::SameLine(400);
        ImGui::Text("Scan Results: %zu entries", g_scanResults.count);
        ImGui::Separator();

        ImGui::Columns(2, "mainColumns", true);
        
        ImGui::Text("Process Control");
        ImGui::Separator();
        
        if (ImGui::Button("List Processes")) {
            showProcessList = true;
        }
        
        static int inputPid = 0;
        ImGui::InputInt("Process ID", &inputPid);
        if (ImGui::Button("Attach to Process")) {
            if (attachToProcess(&g_currentProcess, inputPid)) {
                showProcessList = false;
            }
        }
        
        ImGui::Separator();
        ImGui::Text("Memory Operations");
        
        ImGui::InputInt("Value to Find", &valueToFind);
        
        if (ImGui::BeginCombo("Value Type", valueTypeNames[currentValueType])) {
            for (int i = 0; i < IM_ARRAYSIZE(valueTypeNames); i++) {
                bool isSelected = (currentValueType == i);
                if (ImGui::Selectable(valueTypeNames[i], isSelected)) {
                    currentValueType = (ValueType)i;
                    LOG_DEBUG("Selected value type: %s", valueTypeNames[currentValueType]);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Scan for Value")) {
            if (g_currentProcess.processHandle) {
                freeScanResults(&g_scanResults);
                scanMemory(&g_currentProcess, valueToFind);
            }
        }
        
        ImGui::InputInt("New Value for Filtering", &newValue);
        
        if (ImGui::Button("Narrow Results")) {
            if (g_currentProcess.processHandle && g_scanResults.count > 0) {
                narrowResults(&g_currentProcess, &g_scanResults, newValue);
            }
        }
        
        ImGui::Separator();
        ImGui::Text("Memory Modification");
        
        ImGui::InputText("Address (Hex)", g_addressInput, sizeof(g_addressInput), 
                         ImGuiInputTextFlags_CharsHexadecimal);

        if (ImGui::Button("Write Memory")) {
            if (g_currentProcess.processHandle) {
                DWORD addr;
                if (sscanf(g_addressInput, "%x", &addr) == 1) {
                    updateMemoryValue(&g_currentProcess, addr, newValue);
                } else {
                    ShowStatusMessage("Invalid address format");
                }
            }
        }
        
        if (ImGui::Button("View Memory Regions")) {
            showMemoryRegions = true;
        }
        
        if (g_scanInProgress) {
            if (ImGui::Button("Cancel Scan", ImVec2(120, 0))) {
                g_cancelScan = true;
                LOG_INFO("User requested scan cancellation");
                ShowStatusMessage("Cancelling scan...");
            }
            
            float progress = (g_totalRegionsToScan > 0) ? 
                (float)g_regionsScanned / (float)g_totalRegionsToScan : 0.0f;
            
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
            ImGui::SameLine();
            ImGui::Text("%.1f%%", progress * 100);
        }

        ImGui::NextColumn();
        
        ImGui::Text("Scan Results");
        ImGui::SameLine();
        if (ImGui::Button("Clear Results")) {
            freeScanResults(&g_scanResults);
            g_resultsUpdated = true;
        }
        
        DisplayScanResults(ImGuiTableFlags_Borders | 
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | 
                          ImGuiTableFlags_Reorderable);
        
        ImGui::Separator();

        ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
        
        if (g_scanResults.count > 0) {
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | 
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
            
            if (ImGui::BeginTable("ScanResultsTable", 3, flags, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("Current Value");
                ImGui::TableSetupColumn("Original Value");
                ImGui::TableHeadersRow();
                
                for (size_t i = 0; i < g_scanResults.count; i++) {
                    char addressStr[20];
                    sprintf_s(addressStr, sizeof(addressStr), "0x%08X", g_scanResults.entries[i].address);
                    
                    if (searchBuffer[0] != '\0') {
                        char valueStr[20];
                        sprintf_s(valueStr, sizeof(valueStr), "%d", g_scanResults.entries[i].value);
                        if (strstr(addressStr, searchBuffer) == NULL && 
                            strstr(valueStr, searchBuffer) == NULL) {
                            continue;
                        }
                    }
                    
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(addressStr, false, ImGuiSelectableFlags_SpanAllColumns)) {
                        addressToModify = g_scanResults.entries[i].address;
                        newValue = g_scanResults.entries[i].value;
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", g_scanResults.entries[i].value);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", g_scanResults.entries[i].originalValue);
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No scan results to display.");
            ImGui::TextWrapped("Use 'Scan for Value' to find memory addresses containing a specific value.");
        }
        
        ImGui::Columns(1);

        if (g_statusMessageTime > 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, g_statusMessageTime / 5.0f), 
                               g_statusMessage);
            g_statusMessageTime -= ImGui::GetIO().DeltaTime;
        }
        
        ImGui::End();
        
        if (showProcessList) {
            ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(
                ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                ImGuiCond_FirstUseEver,
                ImVec2(0.5f, 0.5f)
            );
            ImGui::Begin("Process List", &showProcessList, 
                ImGuiWindowFlags_NoSavedSettings | 
                ImGuiWindowFlags_AlwaysAutoResize);
            listProcesses(ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg);
            ImGui::End();
        }

        if (showMemoryRegions) {
            ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(
                ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                ImGuiCond_FirstUseEver,
                ImVec2(0.5f, 0.5f)
            );
            ImGui::Begin("Memory Regions", &showMemoryRegions, 
                ImGuiWindowFlags_NoSavedSettings);
            if (g_currentProcess.processHandle) {
                displayMemoryRegions(&g_currentProcess, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No process attached!");
            }
            ImGui::End();
        }

        if (showSettingsDialog) {
            bool settingsChanged = ShowSettingsDialog(&showSettingsDialog, &g_settings);
            if (settingsChanged) {
                if (g_settings.alwaysOnTop) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                } else {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                }
            }
        }

        if (g_logConsole.getVisible()) {
            bool open = true;
            g_logConsole.draw("Log Console", &open);
            if (!open) {
                g_logConsole.setVisible(false);
            }
        }
		if (g_firstRun && g_settings.showWelcomeGuide) {
            ShowWelcomeGuide();
        }

		if (g_resultsUpdated) {
            UpdateResultsDisplay();
        }

        if (g_statusMessageTime > 0.0f) {
            g_statusMessageTime -= ImGui::GetIO().DeltaTime;
            ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 30));
            ImGui::SetNextWindowBgAlpha(0.35f);
            if (ImGui::Begin("##StatusMessage", nullptr, 
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | 
                ImGuiWindowFlags_NoSavedSettings)) {
                
                ImGui::Text("%s", g_statusMessage);
                ImGui::End();
            }
        }

        ImGui::Render();

        const float clear_color_with_alpha[4] = {0.45f, 0.55f, 0.60f, 1.00f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(g_vsync ? 1 : 0, 0);
    }

    try {
        if (g_currentProcess.processHandle) {
            CloseHandle(g_currentProcess.processHandle);
            g_currentProcess.processHandle = NULL;
        }
        freeScanResults(&g_scanResults);
        
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    }
    catch (const std::exception& e) {
        LOG_CRITICAL("Exception during cleanup: %s", e.what());
        if (g_currentProcess.processHandle) {
            CloseHandle(g_currentProcess.processHandle);
        }
        freeScanResults(&g_scanResults);
    }
    
    return 0;
}


void ShowWelcomeGuide() {
    try {
        static int currentPage = 0;
        static bool welcomeGuideOpen = true;
        static bool initialized = false;
        
        if (!ImGui::GetCurrentContext()) {
            CaptureDebugInfo("ImGui context is null in ShowWelcomeGuide");
            LOG_ERROR("ImGui context is null - cannot show Welcome Guide");
            return;
        }

        if (g_firstRun && g_settings.showWelcomeGuide && !initialized) {
            currentPage = 0;
            welcomeGuideOpen = true;
            initialized = true;
            LOG_DEBUG("Welcome Guide initialized");
        }
        
        if (!g_settings.showWelcomeGuide || !g_firstRun || !welcomeGuideOpen) {
            LOG_DEBUG("Welcome Guide early exit - Settings:%d FirstRun:%d Open:%d",
                     g_settings.showWelcomeGuide, g_firstRun, welcomeGuideOpen);
            return;
        }

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        if (displaySize.x <= 0 || displaySize.y <= 0) {
            CaptureDebugInfo("Invalid display size");
            LOG_ERROR("Invalid display size for Welcome Guide");
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(550, 350), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
            ImGuiCond_FirstUseEver,
            ImVec2(0.5f, 0.5f)
        );

        if (ImGui::Begin("Welcome to CEngine##WelcomeGuide", &welcomeGuideOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

            ImGui::Separator();
            
            switch (currentPage) {
                case 0:
                    ImGui::TextWrapped("Welcome to CEngine Memory Scanner!");
                    ImGui::Spacing();
                    ImGui::TextWrapped("This guide will help you get started with the basic features. "
                                     "You can always access this guide again from the Help menu.");
                    break;
                    
                case 1:
                    ImGui::TextWrapped("Step 1: Process Selection");
                    ImGui::Spacing();
                    ImGui::TextWrapped("To begin scanning, first select a process using either:\n"
                                     "- The 'List Processes' button\n"
                                     "- Enter a Process ID directly\n"
                                     "- Select from the Window Selection dropdown");
                    break;
                    
                case 2:
                    ImGui::TextWrapped("Step 2: Memory Scanning");
                    ImGui::Spacing();
                    ImGui::TextWrapped("Once a process is selected:\n"
                                     "1. Enter a value to search for\n"
                                     "2. Choose the value type (int, float, etc.)\n"
                                     "3. Click 'Scan for Value'\n"
                                     "4. Use 'Narrow Results' with new values to refine your search\n");
					ImGui::Spacing();
					ImGui::Spacing();
					ImGui::TextWrapped("Note: If you choose the auto-detect option, the program will automatically detect the value type\n"
									 "The auto-detect can be inaccurate, so it is recommended to choose the correct value type\n");
                    break;
                    
                case 3:
                    ImGui::TextWrapped("Step 3: Memory Modification");
                    ImGui::Spacing();
                    ImGui::TextWrapped("To modify memory values:\n"
                                     "1. Select an address from the results\n"
                                     "2. Enter a new value\n"
                                     "3. Click 'Write Memory'\n\n"
                                     "Remember to use this tool responsibly!");
                    break;
            }
            
            ImGui::Separator();
            
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
            if (currentPage > 0) {
                if (ImGui::Button("Previous")) {
                    currentPage--;
                }
                ImGui::SameLine();
            }
            
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120);
            if (currentPage < 3) {
                if (ImGui::Button("Next")) {
                    currentPage++;
                }
            } else {
                if (ImGui::Button("Finish")) {
                    welcomeGuideOpen = false;
                }
            }
            
            ImGui::End();
        }

        if (!welcomeGuideOpen) {
            g_settings.showWelcomeGuide = false;
            g_firstRun = false;
            initialized = false;
            
            if (!saveSettings(&g_settings, getSettingsFilePath())) {
                CaptureDebugInfo("Failed to save settings after closing Welcome Guide");
                LOG_ERROR("Failed to save settings after closing Welcome Guide");
            }
        }

    } catch (const std::exception& e) {
        CaptureDebugInfo(e.what());
        LOG_ERROR("Exception in Welcome Guide: %s", e.what());
        ShowStatusMessage("Error showing Welcome Guide - check log for details");
    }
}

void listProcesses(ImGuiTableFlags flags) {
    static HWND selectedWindow = NULL;
    if (ImGui::BeginCombo("Window Selection", "Select Window...")) {
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;
            
            char title[256];
            if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
                DWORD processId;
                GetWindowThreadProcessId(hwnd, &processId);
                
                char menuItem[512];
                snprintf(menuItem, sizeof(menuItem), "%s (PID: %lu)", title, processId);
                
                if (ImGui::Selectable(menuItem)) {
                    if (attachToProcess(&g_currentProcess, processId)) {
                        showProcessList = false;
                    }
                }
            }
            return TRUE;
        }, 0);
        ImGui::EndCombo();
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to create process snapshot");
        return;
    }

    static char processFilter[256] = "";
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    
    ImGui::InputText("Filter", processFilter, sizeof(processFilter));

    if (ImGui::BeginTable("ProcessTable", 4, flags, ImVec2(0, 300))) {
        ImGui::TableSetupColumn("PID");
        ImGui::TableSetupColumn("Process Name");
        ImGui::TableSetupColumn("Memory Usage");
        ImGui::TableSetupColumn("Type");
        ImGui::TableHeadersRow();
        
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                char processName[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, 
                                  processName, sizeof(processName), NULL, NULL);
                
                if (processFilter[0] != '\0' && 
                    !strstr(processName, processFilter)) {
                    continue;
                }

                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | 
                                           PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(std::to_string(pe32.th32ProcessID).c_str(), 
                    false, ImGuiSelectableFlags_SpanAllColumns)) {
                    attachToProcess(&g_currentProcess, pe32.th32ProcessID);
                    showProcessList = false;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", processName);

                ImGui::TableSetColumnIndex(2);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        ImGui::Text("%.2f MB", pmc.WorkingSetSize / (1024.0f * 1024.0f));
                    } else {
                        ImGui::Text("N/A");
                    }
                } else {
                    ImGui::Text("N/A");
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", pe32.th32ParentProcessID == 0 ? "System" : "User");

                if (hProcess) {
                    CloseHandle(hProcess);
                }

            } while (Process32NextW(hSnapshot, &pe32));
        }
        
        ImGui::EndTable();
    }
    
    CloseHandle(hSnapshot);
}

bool attachToProcess(ProcessInfo* process, DWORD processId) {
    if (!process) {
        LOG_ERROR("Null process pointer");
        return FALSE;
    }

    process->settings = &g_settings;

    if (processId == 0 || processId == 4 || processId == 8) {
        LOG_ERROR("Cannot attach to system process (PID: %lu)", processId);
        ShowFormattedStatusMessage("Cannot attach to system process (PID: %lu)", processId);
        return FALSE;
    }
    
    if (processId == GetCurrentProcessId()) {
        LOG_WARNING("Cannot attach to own process");
        ShowStatusMessage("Cannot attach to own process");
        return FALSE;
    }
    
    if (process->processHandle) {
        CloseHandle(process->processHandle);
        process->processHandle = NULL;
        process->processId = 0;
        process->settings = &g_settings;
        ZeroMemory(process->processName, sizeof(process->processName));
    }
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to create process snapshot");
        return FALSE;
    }
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        LOG_ERROR("Failed to get first process in snapshot");
        return FALSE;
    }
    
    BOOL found = FALSE;
    BOOL isSystemProcess = FALSE;
    
    do {
        if (pe32.th32ProcessID == processId) {
            char exeName[MAX_PATH] = {0};
            if (WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, 
                                  exeName, sizeof(exeName)-1, NULL, NULL) == 0) {
                LOG_ERROR("Failed to convert process name");
                CloseHandle(hSnapshot);
                return FALSE;
            }
            
            const char* protectedProcesses[] = {
                "explorer.exe", "lsass.exe", "csrss.exe", "winlogon.exe", 
                "services.exe", "smss.exe", "svchost.exe", "wininit.exe"
            };
            
            for (int i = 0; i < sizeof(protectedProcesses)/sizeof(protectedProcesses[0]); i++) {
                if (_stricmp(exeName, protectedProcesses[i]) == 0) {
                    LOG_WARNING("System process detected: %s", exeName);
                    
                    if (!g_settings.allowSystemProcesses) {
                        char message[256];
                        snprintf(message, sizeof(message), 
                               "%s is a system process.\nModifying it may cause system instability.\n\nContinue anyway?", 
                               exeName);
                        
                        int result = MessageBoxA(NULL, message, "Security Warning", 
                                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                        
                        if (result == IDNO) {
                            CloseHandle(hSnapshot);
                            return FALSE;
                        }
                    }
                    
                    isSystemProcess = TRUE;
                    break;
                }
            }

            strncpy_s(process->processName, sizeof(process->processName), 
                     exeName, _TRUNCATE);
            process->processId = processId;
            found = TRUE;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));
    
    CloseHandle(hSnapshot);
    
    if (!found) {
        LOG_ERROR("Process ID %lu not found", processId);
        return FALSE;
    }

    DWORD accessFlags = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
    
    process->processHandle = OpenProcess(accessFlags, FALSE, processId);
    
    if (process->processHandle == NULL) {
        DWORD error = GetLastError();
        
        if (error == ERROR_ACCESS_DENIED) {
            if (g_settings.automaticPrivilegeElevation) {
                LOG_WARNING("Limited access denied for process %lu, trying with higher privileges", processId);
                process->processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
                if (process->processHandle) {
                    LOG_WARNING("Using higher privileges for process %lu", processId);
                }
            } else {
                char message[256];
                snprintf(message, sizeof(message), 
                       "Access denied for process %s (PID: %lu).\n\nTry with elevated privileges?", 
                       process->processName, processId);
                
                int result = MessageBoxA(NULL, message, "Access Denied", 
                                      MB_YESNO | MB_ICONQUESTION);
                
                if (result == IDYES) {
                    LOG_WARNING("User requested elevated access for process %lu", processId);
                    process->processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
                    if (process->processHandle) {
                        LOG_SECURITY("Elevated privileges used for %s (PID: %lu)", 
                                   process->processName, processId);
                    }
                }
            }
        }
        
        if (process->processHandle == NULL) {
            LOG_ERROR("Failed to open process %lu, error code: %lu", processId, GetLastError());
            return FALSE;
        }
    }
    
    LOG_INFO("Successfully attached to process %s (PID: %lu)", 
            process->processName, process->processId);
    return TRUE;
}

void initScanResults(ScanResults* results) {
    results->entries = NULL;
    results->count = 0;
    results->capacity = 0;
}

void optimizeScanResults(ScanResults* results) {
    std::lock_guard<std::mutex> lock(scanResultsMutex);
    
    if (results->count < results->capacity / 2 && results->count > 0) {
        size_t newCapacity = (1024 > results->count * 2) ? 1024 : results->count * 2;
        
        if (newCapacity == 0) newCapacity = 1024;
        
        MemoryEntry* newEntries = (MemoryEntry*)realloc(
            results->entries, newCapacity * sizeof(MemoryEntry));
            
        if (newEntries) {
            results->entries = newEntries;
            results->capacity = newCapacity;
            LOG_DEBUG("Optimized results: new capacity %zu", newCapacity);
        }
        else {
            LOG_WARNING("Failed to reallocate memory during results optimization");
        }
    }
}

bool addScanResult(ScanResults* results, DWORD address, int value) {
    if (!results->entries) {
        results->entries = (MemoryEntry*)malloc(INITIAL_RESULTS_CAPACITY * sizeof(MemoryEntry));
        if (!results->entries) {
            LOG_ERROR("Failed to initialize results array");
            return false;
        }
        results->capacity = INITIAL_RESULTS_CAPACITY;
        results->count = 0;
        LOG_DEBUG("Initialized results array with capacity %zu", results->capacity);
    }
    
    if (results->count >= results->capacity) {
        size_t newCapacity = results->capacity * 2;
        MemoryEntry* newEntries = (MemoryEntry*)realloc(results->entries, 
                                                       newCapacity * sizeof(MemoryEntry));
        if (!newEntries) {
            LOG_ERROR("Failed to expand results array");
            return false;
        }
        results->entries = newEntries;
        results->capacity = newCapacity;
        LOG_DEBUG("Expanded results array to capacity %zu", newCapacity);
    }

    results->entries[results->count].address = address;
    results->entries[results->count].value = value;
    results->entries[results->count].originalValue = value;
    results->count++;
    
    LOG_DEBUG("Added result: Address=0x%08X Value=%d (Total: %zu)", 
              address, value, results->count);
    return true;
}

void freeScanResults(ScanResults* results) {
    if (results->entries) {
        free(results->entries);
        results->entries = NULL;
    }
    results->count = 0;
    results->capacity = 0;
}

std::atomic<bool> g_threadAlive{true};
const DWORD THREAD_CHECK_INTERVAL = 500; // milliseconds

int GetValueTypeSize(ValueType type) {
    switch (type) {
        case VALUE_TYPE_INT: return sizeof(int);
        case VALUE_TYPE_FLOAT: return sizeof(float);
        case VALUE_TYPE_DOUBLE: return sizeof(double);
        case VALUE_TYPE_SHORT: return sizeof(short);
        case VALUE_TYPE_BYTE: return sizeof(char);
        case VALUE_TYPE_AUTO: return sizeof(int);
    }
    return sizeof(int);
}

bool ValueMatches(const BYTE* buffer, int valueToFind, ValueType type) {
    switch (type) {
        case VALUE_TYPE_AUTO: {
            int ivalue;
            float fvalue;
            double dvalue;
            short svalue;
            unsigned char bvalue;
            
            memcpy(&ivalue, buffer, sizeof(int));
            if (ivalue == valueToFind) return true;
            
            memcpy(&fvalue, buffer, sizeof(float));
            if (!std::isnan(fvalue) && !std::isinf(fvalue) && 
                std::abs(fvalue - (float)valueToFind) < 0.0001f) {
                uint32_t bits;
                memcpy(&bits, &fvalue, sizeof(bits));
                uint32_t exp = (bits >> 23) & 0xFF;
                if (exp != 0 && exp != 0xFF);
            }
            
            memcpy(&dvalue, buffer, sizeof(double));
            if (!std::isnan(dvalue) && !std::isinf(dvalue) && 
                std::abs(dvalue - (double)valueToFind) < 0.0001) {
                uint64_t bits;
                memcpy(&bits, &dvalue, sizeof(bits));
                uint64_t exp = (bits >> 52) & 0x7FF;
                if (exp != 0 && exp != 0x7FF)
                    return true;
            }
            
            memcpy(&svalue, buffer, sizeof(short));
            if (svalue == (short)valueToFind) return true;
            
            memcpy(&bvalue, buffer, sizeof(unsigned char));
            if (bvalue == (unsigned char)valueToFind) return true;
            
            return false;
        }
            
        case VALUE_TYPE_INT: {
            int value;
            memcpy(&value, buffer, sizeof(int));
            return value == valueToFind;
        }
            
        case VALUE_TYPE_FLOAT: {
            float fvalue;
            memcpy(&fvalue, buffer, sizeof(float));
            if (std::isnan(fvalue) || std::isinf(fvalue)) 
                return false;
            return std::abs(fvalue - (float)valueToFind) < 0.0001f;
        }
            
        case VALUE_TYPE_DOUBLE: {
            double dvalue;
            memcpy(&dvalue, buffer, sizeof(double));
            if (std::isnan(dvalue) || std::isinf(dvalue))
                return false;
            return std::abs(dvalue - (double)valueToFind) < 0.0001;
        }
            
        case VALUE_TYPE_SHORT: {
            short value;
            memcpy(&value, buffer, sizeof(short));
            return value == (short)valueToFind;
        }
            
        case VALUE_TYPE_BYTE: {
            unsigned char value;
            memcpy(&value, buffer, sizeof(unsigned char));
            return value == (unsigned char)valueToFind;
        }
    }
    return false;
}

bool VectorizedValueMatch(const BYTE* buffer, int valueToFind, ValueType type, SIZE_T bufferSize) {
    if (type == VALUE_TYPE_INT && bufferSize >= 16) {
        __m128i searchValue = _mm_set1_epi32(valueToFind);
        
        for (SIZE_T i = 0; i <= bufferSize - 16; i += 16) {
            __m128i data = _mm_loadu_si128((__m128i*)&buffer[i]);
            __m128i cmp = _mm_cmpeq_epi32(data, searchValue);
            
            int mask = _mm_movemask_epi8(cmp);
            if (mask != 0) {
                for (int j = 0; j < 4; j++) {
                    int value;
                    memcpy(&value, &buffer[i + j * sizeof(int)], sizeof(int));
                    if (value == valueToFind) {
                        return true;
                    }
                }
            }
        }
    }
    
    int typeSize = GetValueTypeSize(type);
    for (SIZE_T i = 0; i <= bufferSize - typeSize; i++) {
        if (ValueMatches(&buffer[i], valueToFind, type)) {
            return true;
        }
    }
    
    return false;
}

bool IsLikelyValidDataRegion(const MEMORY_BASIC_INFORMATION& mbi, bool isSearchingForZero) {
    if (mbi.RegionSize < 4096)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
        
    if (isSearchingForZero) {
        if (mbi.Type == MEM_MAPPED)
            return false;           
        if (mbi.Type != MEM_PRIVATE)
            return false;
    }
    
    return true;
}

void validateScanResults() {
    LOG_DEBUG("Validating scan results...");
    if (!g_scanResults.entries && g_scanResults.count > 0) {
        LOG_ERROR("Invalid scan results: null entries but count > 0");
        g_scanResults.count = 0;
        return;
    }
    
    if (g_scanResults.entries) {
        LOG_DEBUG("Scan results: %zu entries, capacity %zu", 
                 g_scanResults.count, g_scanResults.capacity);
        if (g_scanResults.count > 0) {
            LOG_DEBUG("First entry: 0x%08X = %d", 
                     g_scanResults.entries[0].address,
                     g_scanResults.entries[0].value);
            LOG_DEBUG("Last entry: 0x%08X = %d",
                     g_scanResults.entries[g_scanResults.count-1].address,
                     g_scanResults.entries[g_scanResults.count-1].value);
        }    
    }
}

bool IsReadableMemory(HANDLE processHandle, LPCVOID address, SIZE_T size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    return (mbi.State == MEM_COMMIT) && 
           (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
           !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

bool SafeReadMemory(HANDLE processHandle, LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead) {
    *bytesRead = 0;
    if (!IsReadableMemory(processHandle, address, size)) {
        return false;
    }
    return ReadProcessMemory(processHandle, address, buffer, size, bytesRead);
}

void scanMemory(ProcessInfo* process, int valueToFind) {
    if (!process || !process->processHandle) {
        LOG_ERROR("Invalid process handle");
        ShowStatusMessage("Invalid process handle");
        return;
    }

    freeScanResults(&g_scanResults);
    initScanResults(&g_scanResults);

    g_cancelScan = false;
    g_scanInProgress = true;
    g_regionsScanned = 0;
    g_bytesScanned = 0;
    g_matchesFound = 0;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    LPVOID address = sysInfo.lpMinimumApplicationAddress;
    
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    
    while (address < sysInfo.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(process->processHandle, address, &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (IsLikelyValidDataRegion(mbi, valueToFind == 0)) {
            regions.push_back(mbi);
        }

        address = (LPVOID)((DWORD_PTR)mbi.BaseAddress + mbi.RegionSize);
    }

    g_totalRegionsToScan = regions.size();
    LOG_INFO("Found %zu memory regions to scan", g_totalRegionsToScan);

    std::vector<BYTE> buffer(SCAN_CHUNK_SIZE);
    
    for (const auto& mbi : regions) {
        if (g_cancelScan) break;

        SIZE_T bytesRead = 0;
        for (SIZE_T offset = 0; offset < mbi.RegionSize; offset += SCAN_CHUNK_SIZE) {
            SIZE_T bytesToRead = min_val(SCAN_CHUNK_SIZE, mbi.RegionSize - offset);
            LPVOID currentAddress = (LPVOID)((DWORD_PTR)mbi.BaseAddress + offset);

            if (SafeReadMemory(process->processHandle, currentAddress, buffer.data(), bytesToRead, &bytesRead)) {
                for (SIZE_T i = 0; i <= bytesRead - GetValueTypeSize(currentValueType); i++) {
                    if (ValueMatches(&buffer[i], valueToFind, currentValueType)) {
                        DWORD resultAddress = (DWORD)((DWORD_PTR)currentAddress + i);
                        addScanResult(&g_scanResults, resultAddress, valueToFind);
                    }
                }

                if (bytesRead >= sizeof(DWORD)) {
                    for (SIZE_T i = 0; i <= bytesRead - sizeof(DWORD); i += sizeof(DWORD)) {
                        DWORD pointerValue;
                        memcpy(&pointerValue, &buffer[i], sizeof(DWORD));

                        DWORD minAppAddr = (DWORD)(DWORD_PTR)sysInfo.lpMinimumApplicationAddress;
                        DWORD maxAppAddr = (DWORD)(DWORD_PTR)sysInfo.lpMaximumApplicationAddress;
                        
                        if (pointerValue > minAppAddr && pointerValue < maxAppAddr) {
                            
                            int pointedValue;
                            SIZE_T pointedBytesRead;
                            if (SafeReadMemory(process->processHandle, (LPCVOID)(DWORD_PTR)pointerValue, 
                                            &pointedValue, sizeof(int), &pointedBytesRead) && 
                                pointedValue == valueToFind) {
                                    
                                DWORD pointerAddress = (DWORD)((DWORD_PTR)currentAddress + i);
                                addScanResult(&g_scanResults, pointerAddress, pointerValue);
                            }
                        }
                    }
                }
            }

            g_bytesScanned += bytesRead;
        }

        g_regionsScanned++;
        g_scanProgress = (double)g_regionsScanned / g_totalRegionsToScan;
    }

    g_scanInProgress = false;
    validateScanResults();
    
    size_t resultCount = g_scanResults.count;
    size_t regionsScannedCount = g_regionsScanned.load();
    LOG_INFO("Scan completed: Found %zu matches in %zu regions", 
             resultCount, regionsScannedCount);
    
    ShowFormattedStatusMessage("Found %zu matches", resultCount);
}

void narrowResults(ProcessInfo* process, ScanResults* results, int newValue) {
    if (!process || !process->processHandle || results->count == 0) {
        LOG_WARNING("Cannot narrow results: invalid process or empty results");
        return;
    }
    
    LOG_INFO("Narrowing results from %zu entries with value %d", results->count, newValue);

    std::unique_ptr<MemoryEntry[]> tempEntries(new (std::nothrow) MemoryEntry[results->count]);
    if (!tempEntries) {
        LOG_ERROR("Failed to allocate memory for narrowing results");
        return;
    }

    g_cancelScan = false;
    g_scanInProgress = true;
    g_regionsScanned = 0;
    g_totalRegionsToScan = results->count;
    
    size_t tempCount = 0;
    size_t entriesRemoved = 0;

    const DWORD TIMEOUT_MS = 5000; // 5 second timeout
    const DWORD CHECK_INTERVAL = 100; // Check every 100ms
    DWORD startTime = GetTickCount();

    const size_t BATCH_SIZE = 500;
    std::vector<std::pair<DWORD, int>> validResults;
    validResults.reserve(BATCH_SIZE);
    
    for (size_t i = 0; i < results->count && !g_cancelScan; i++) {
        if ((i % 100) == 0) {
            DWORD currentTime = GetTickCount();
            if (currentTime - startTime > TIMEOUT_MS) {
                LOG_WARNING("Narrowing operation timed out after processing %zu entries", i);
                ShowStatusMessage("Operation timed out - partial results saved");
                break;
            }

            MSG msg;
            while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            Sleep(0);
        }
        
        DWORD address = results->entries[i].address;
        SIZE_T bytesRead = 0;
        bool valueMatches = false;
        
        try {
            const DWORD READ_TIMEOUT = 50; // 50ms per read
            DWORD readStart = GetTickCount();
            
            switch (currentValueType) {
                case VALUE_TYPE_INT: {
                    int value;
                    if (ReadProcessMemory(process->processHandle, (LPVOID)(uintptr_t)address, 
                                       &value, sizeof(value), &bytesRead) && 
                        bytesRead == sizeof(value)) {
                        valueMatches = (value == newValue);
                        if (valueMatches) {
                            validResults.emplace_back(address, value);
                        }
                    }
                    break;
                }
            }
            
            if (GetTickCount() - readStart > READ_TIMEOUT) {
                LOG_DEBUG("Slow memory read at address 0x%08X", address);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing address 0x%08X: %s", address, e.what());
            continue;
        }

        if (validResults.size() >= BATCH_SIZE) {
            std::lock_guard<std::mutex> lock(scanResultsMutex);
            for (const auto& result : validResults) {
                tempEntries[tempCount++] = {
                    result.first,
                    result.second,
                    results->entries[i].originalValue
                };
            }
            validResults.clear();

            g_regionsScanned = i;
            float progress = (float)i / results->count;
            ShowFormattedStatusMessage("Narrowing results: %.1f%% (Found: %zu)", 
                            progress * 100.0f, tempCount);
        }
    }

    if (!validResults.empty()) {
        std::lock_guard<std::mutex> lock(scanResultsMutex);
        size_t currentIndex = g_regionsScanned - validResults.size();
        for (size_t j = 0; j < validResults.size(); j++) {
            tempEntries[tempCount++] = {
                validResults[j].first,
                validResults[j].second,
                results->entries[currentIndex + j].originalValue
            };
        }
    }

    {
        std::lock_guard<std::mutex> lock(scanResultsMutex);
        if (results->entries) {
            free(results->entries);
        }
        results->entries = (MemoryEntry*)malloc(tempCount * sizeof(MemoryEntry));
        if (results->entries) {
            memcpy(results->entries, tempEntries.get(), tempCount * sizeof(MemoryEntry));
            results->count = tempCount;
            results->capacity = tempCount;
        } else {
            LOG_ERROR("Failed to allocate memory for narrowed results");
            results->count = 0;
            results->capacity = 0;
        }
    }
    
    g_scanInProgress = false;
    g_resultsUpdated = true;
    
    LOG_INFO("Narrowing complete. Removed %zu entries, kept %zu entries.", 
             results->count - tempCount, tempCount);
    ShowFormattedStatusMessage("Narrowed to %zu results", tempCount);
}

void updateMemoryValue(ProcessInfo* process, DWORD address, int newValue) {
    if (!process || !process->processHandle) {
        LOG_ERROR("Invalid process for memory write");
        ShowStatusMessage("Invalid process for memory write");
        return;
    }

    HANDLE processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 
                                     FALSE, process->processId);
    if (!processHandle) {
        LOG_ERROR("Failed to open process with write permissions");
        ShowStatusMessage("Failed to get write permissions");
        return;
    }

    MemoryProtectionContext* protContext = CreateProtectionContext(processHandle, 
        (LPVOID)(uintptr_t)address, sizeof(int));

    bool success = false;
    
    MEMORY_BASIC_INFORMATION mbi;
    bool needProtectionChange = false;
    
    if (VirtualQueryEx(processHandle, (LPVOID)(uintptr_t)address, &mbi, sizeof(mbi))) {
        LOG_DEBUG("Memory at 0x%08X has protection: 0x%X", address, mbi.Protect);
        
        if (!(mbi.Protect & PAGE_READWRITE) && 
            !(mbi.Protect & PAGE_EXECUTE_READWRITE) &&
            !(mbi.Protect & PAGE_WRITECOPY) &&
            !(mbi.Protect & PAGE_EXECUTE_WRITECOPY)) {
            
            needProtectionChange = true;
        }
    }
    
    if (needProtectionChange && g_settings.overwriteMemoryProtection) {
        LOG_DEBUG("Attempting to modify memory protection for address 0x%08X", address);
        
        if (!ModifyMemoryProtection(protContext, PAGE_READWRITE)) {
            LOG_ERROR("Failed to modify memory protection for address 0x%08X", address);
            ShowFormattedStatusMessage("Failed to modify memory protection for address 0x%08X", address);
            DestroyProtectionContext(protContext);
            CloseHandle(processHandle);
            return;
        }
        
        LOG_DEBUG("Successfully modified memory protection for address 0x%08X", address);
    }

    SIZE_T bytesWritten;
    if (WriteProcessMemory(processHandle, (LPVOID)(uintptr_t)address, 
                          &newValue, sizeof(newValue), &bytesWritten)) {
        LOG_INFO("Successfully wrote value %d to address 0x%08X", newValue, address);
        ShowFormattedStatusMessage("Value %d written successfully to 0x%08X", newValue, address);
        success = true;
        
        int verifyValue;
        SIZE_T verifyBytesRead;
        if (SafeReadMemoryWithRetry(processHandle, (LPVOID)(uintptr_t)address, 
                                 &verifyValue, sizeof(verifyValue), &verifyBytesRead, 3)) {
            if (verifyValue == newValue) {
                LOG_DEBUG("Write verified successfully: value %d at 0x%08X", verifyValue, address);
            } else {
                LOG_WARNING("Write verification failed: expected %d, read %d at 0x%08X", 
                           newValue, verifyValue, address);
            }
        }
    } else {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to write to address 0x%08X (Error: %lu - %s)", 
                address, error, GetLastErrorAsString(error));
        ShowFormattedStatusMessage("Failed to write value: %s", GetLastErrorAsString(error));
    }

    RestoreMemoryProtection(protContext);
    
    DestroyProtectionContext(protContext);
    CloseHandle(processHandle);
}

void displayMemoryRegions(ProcessInfo* process, ImGuiTableFlags flags) {
    if (!process || !process->processHandle) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No valid process attached!");
        return;
    }
    
    ImGui::TextWrapped("Memory regions for: %s (PID: %lu)", 
                      process->processName, process->processId);
    
    static bool showCommitted = true;
    static bool showFree = false;
    static bool showReserved = false;
    static bool showReadOnly = true;
    static bool showReadWrite = true;
    static bool showExecutable = true;
    static bool showProtected = false;
    
    ImGui::Text("Filter options:");
    
    ImGui::Checkbox("Committed", &showCommitted);
    ImGui::SameLine();
    ImGui::Checkbox("Free", &showFree);
    ImGui::SameLine();
    ImGui::Checkbox("Reserved", &showReserved);
    
    ImGui::Checkbox("Read-Only", &showReadOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Read-Write", &showReadWrite);
    ImGui::SameLine();
    ImGui::Checkbox("Executable", &showExecutable);
    ImGui::SameLine();
    ImGui::Checkbox("Protected", &showProtected);
    
    static char addressSearch[32] = "";
    ImGui::InputText("Search Address", addressSearch, sizeof(addressSearch), ImGuiInputTextFlags_CharsHexadecimal);
    
    MEMORY_BASIC_INFORMATION mbi;
    LPVOID address = NULL;
    
    if (ImGui::BeginTable("MemoryRegionsTable", 5, flags, ImVec2(0, 450))) {
        ImGui::TableSetupColumn("Base Address", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Region Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Protect", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        
        size_t regionsDisplayed = 0;
        const size_t MAX_REGIONS = 2000; 
        
        try {
            while (regionsDisplayed < MAX_REGIONS && 
                  VirtualQueryEx(process->processHandle, address, &mbi, sizeof(mbi))) {
                
                bool showRegion = false;
                
                if ((mbi.State == MEM_COMMIT && showCommitted) ||
                    (mbi.State == MEM_FREE && showFree) ||
                    (mbi.State == MEM_RESERVE && showReserved)) {
                    
                    if (mbi.State != MEM_FREE) {
                        if ((mbi.Protect & PAGE_READONLY && showReadOnly) ||
                            ((mbi.Protect & PAGE_READWRITE || mbi.Protect & PAGE_WRITECOPY) && showReadWrite) ||
                            ((mbi.Protect & PAGE_EXECUTE || mbi.Protect & PAGE_EXECUTE_READ || 
                              mbi.Protect & PAGE_EXECUTE_READWRITE || mbi.Protect & PAGE_EXECUTE_WRITECOPY) && 
                             showExecutable) ||
                            ((mbi.Protect & PAGE_GUARD || mbi.Protect & PAGE_NOACCESS) && showProtected)) {
                            
                            showRegion = true;
                        }
                    } else {
                        showRegion = true;
                    }
                }
                
                if (addressSearch[0] != '\0') {
                    DWORD_PTR searchAddr = 0;
                    if (sscanf(addressSearch, "%lx", &searchAddr) == 1) {
                        if (!((DWORD_PTR)mbi.BaseAddress <= searchAddr && 
                              searchAddr < (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize)) {
                            showRegion = false;
                        }
                    }
                }
                
                if (showRegion) {
                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    char addressStr[32];
                    snprintf(addressStr, sizeof(addressStr), "0x%p", mbi.BaseAddress);
                    ImGui::Text("%s", addressStr);
                    
                    ImGui::TableSetColumnIndex(1);
                    char sizeStr[32];
                    if (mbi.RegionSize >= 1024*1024) {
                        snprintf(sizeStr, sizeof(sizeStr), "%.2f MB", (float)mbi.RegionSize / (1024*1024));
                    } else if (mbi.RegionSize >= 1024) {
                        snprintf(sizeStr, sizeof(sizeStr), "%.2f KB", (float)mbi.RegionSize / 1024);
                    } else {
                        snprintf(sizeStr, sizeof(sizeStr), "%zu B", mbi.RegionSize);
                    }
                    ImGui::Text("%s", sizeStr);
                    
                    ImGui::TableSetColumnIndex(2);
                    if (mbi.State == MEM_COMMIT) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "COMMIT");
                    }
                    else if (mbi.State == MEM_FREE) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "FREE");
                    }
                    else if (mbi.State == MEM_RESERVE) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "RESERVE");
                    }
                    
                    ImGui::TableSetColumnIndex(3);
                    if (mbi.State != MEM_FREE) {
                        if (mbi.Protect & PAGE_NOACCESS) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "NOACCESS");
                        }
                        else if (mbi.Protect & PAGE_READONLY) {
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "READONLY");
                        }
                        else if (mbi.Protect & PAGE_READWRITE) {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "READWRITE");
                        }
                        else if (mbi.Protect & PAGE_WRITECOPY) {
                            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.8f, 1.0f), "WRITECOPY");
                        }
                        else if (mbi.Protect & PAGE_EXECUTE) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "EXECUTE");
                        }
                        else if (mbi.Protect & PAGE_EXECUTE_READ) {
                            ImGui::TextColored(ImVec4(0.8f, 0.0f, 0.8f, 1.0f), "EXEC+READ");
                        }
                        else if (mbi.Protect & PAGE_EXECUTE_READWRITE) {
                            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "EXEC+RW");
                        }
                        else if (mbi.Protect & PAGE_EXECUTE_WRITECOPY) {
                            ImGui::TextColored(ImVec4(0.0f, 0.6f, 0.6f, 1.0f), "EXEC+WC");
                        }
                        else {
                            ImGui::Text("0x%X", mbi.Protect);
                        }
                        
                        if (mbi.Protect & PAGE_GUARD) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "+GUARD");
                        }
                        if (mbi.Protect & PAGE_NOCACHE) {
                            ImGui::SameLine();
                            ImGui::Text("+NOCACHE");
                        }
                    } else {
                        ImGui::Text("-");
                    }
                    
                    ImGui::TableSetColumnIndex(4);
                    if (mbi.State != MEM_FREE) {
                        if (mbi.Type == MEM_IMAGE) {
                            ImGui::Text("IMAGE");
                        }
                        else if (mbi.Type == MEM_MAPPED) {
                            ImGui::Text("MAPPED");
                        }
                        else if (mbi.Type == MEM_PRIVATE) {
                            ImGui::Text("PRIVATE");
                        }
                        else {
                            ImGui::Text("0x%X", mbi.Type);
                        }
                    } else {
                        ImGui::Text("-");
                    }
                    
                    regionsDisplayed++;
                }
                
                address = (LPVOID)((DWORD_PTR)mbi.BaseAddress + mbi.RegionSize);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in memory region display: %s", e.what());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
        }
        
        if (regionsDisplayed >= MAX_REGIONS) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), 
                              "Warning: Only showing first %zu regions. Use filters to narrow results.",
                              MAX_REGIONS);
        }
        
        ImGui::EndTable();
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

void ShowStatusMessage(const char* message) {
    if (!message) return;
    
    strncpy_s(g_statusMessage, sizeof(g_statusMessage), message, _TRUNCATE);
    g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';
    
    const float MIN_DISPLAY_TIME = 1.0f;
    const float MAX_DISPLAY_TIME = 10.0f;
    g_statusMessageTime = std::min(std::max(5.0f, MIN_DISPLAY_TIME), MAX_DISPLAY_TIME);
    
    try {
        LOG_INFO("Status: %s", g_statusMessage);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to log status message: %s", e.what());
    }
}

void ShowFormattedStatusMessage(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    ShowStatusMessage(buffer);
}

void UpdateResultsDisplay() {
    if (g_resultsUpdated.exchange(false)) {
        LOG_DEBUG("Updating results display with %zu entries", g_scanResults.count);
        
        if (g_totalRegionsToScan > 0) {
            size_t regionsScanned = g_regionsScanned.load();
            float progress = (float)regionsScanned / (float)g_totalRegionsToScan;
            ShowFormattedStatusMessage("Scan in progress: %zu results found (%.1f%% complete)",
                             g_scanResults.count, progress * 100.0f);
        }
        
        if (g_scanResults.count > 100000 && g_optimizeScanning) {
            LOG_INFO("Optimizing large result set (%zu entries)", g_scanResults.count);
            optimizeScanResults(&g_scanResults);
        }
    }
}

inline float GetScanProgress() {
    return (g_totalRegionsToScan > 0) ? 
        (float)g_regionsScanned / (float)g_totalRegionsToScan : 0.0f;
}


bool isValidMemoryRegion(HANDLE processHandle, LPVOID address, SIZE_T size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(processHandle, address, &mbi, sizeof(mbi))) {
        return false;
    }
    
    if (mbi.State != MEM_COMMIT || 
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | 
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) || 
        (mbi.Protect & PAGE_GUARD) || 
        (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }
    
    return true;
}

unsigned __stdcall scanMemoryThreadFunc(void* arg) {
    ScanThreadData* data = static_cast<ScanThreadData*>(arg);
    if (!data || !data->process || !data->settings || !data->results) {
        LOG_ERROR("Invalid thread data");
        return 1;
    }
    
    LOG_DEBUG("Thread started - Assigned %zu regions to scan", data->regions.size());
    DWORD threadId = GetCurrentThreadId();
    
    HANDLE processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, 
                                     FALSE, data->process->processId);
    if (!processHandle) {
        LOG_ERROR("Thread %lu: Failed to open process for scanning", threadId);
        return 1;
    }
    
    std::vector<std::pair<DWORD, int>> localResults;
    localResults.reserve(SCAN_BATCH_SIZE);
    
    size_t threadBytesScanned = 0;
    size_t threadMatchesFound = 0;
    size_t threadRegionsSkipped = 0;
    SIZE_T bytesRead = 0;
    
    try {
        const int valueTypeSize = GetValueTypeSize(currentValueType);
        const size_t BUFFER_SIZE = static_cast<size_t>(data->settings->scanBufferMB) * 1024 * 1024;
        const size_t PAGE_SIZE = 4096;
        
        std::vector<BYTE> buffer;
        buffer.reserve(BUFFER_SIZE + PAGE_SIZE);
        size_t alignedSize = (BUFFER_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        buffer.resize(alignedSize);

        bool isSearchingForZero = (data->valueToFind == 0);

        std::vector<MEMORY_BASIC_INFORMATION> sortedRegions;
        for (const auto& mbi : data->regions) {
            if (IsLikelyValidDataRegion(mbi, isSearchingForZero)) {
                sortedRegions.push_back(mbi);
            } else {
                g_regionsSkipped++;
                threadRegionsSkipped++;
            }
        }


        std::sort(sortedRegions.begin(), sortedRegions.end(), 
            [](const MEMORY_BASIC_INFORMATION& a, const MEMORY_BASIC_INFORMATION& b) {
                if ((a.Protect & PAGE_READWRITE) && a.Type == MEM_PRIVATE) {
                    if (!((b.Protect & PAGE_READWRITE) && b.Type == MEM_PRIVATE)) {
                        return true;
                    }
                } else if ((b.Protect & PAGE_READWRITE) && b.Type == MEM_PRIVATE) {
                    return false;
                }
                
                return a.RegionSize > b.RegionSize;
            });

        LOG_DEBUG("Thread %lu: Will scan %zu regions after filtering", threadId, sortedRegions.size());

        if (data->settings->prefetchMemory) {
            _mm_prefetch(reinterpret_cast<const char*>(buffer.data()), _MM_HINT_T0);
        }

        for (const auto& mbi : sortedRegions) {
            if (g_cancelScan) break;

            BYTE* currentAddr = static_cast<BYTE*>(mbi.BaseAddress);
            SIZE_T remaining = mbi.RegionSize;
            
            if (!IsLikelyValidDataRegion(mbi, isSearchingForZero)) {
                g_regionsSkipped++;
                threadRegionsSkipped++;
                continue;
            }

            BYTE testBuffer[16];
            if (!ReadProcessMemory(processHandle, currentAddr, testBuffer, sizeof(testBuffer), &bytesRead) || bytesRead == 0) {
                g_regionsSkipped++;
                threadRegionsSkipped++;
                continue;
            }

            if (isSearchingForZero) {
                bool allZeros = true;
                for (size_t i = 0; i < bytesRead; i++) {
                    if (testBuffer[i] != 0) {
                        allZeros = false;
                        break;
                    }
                }

                if (allZeros) {
                    localResults.emplace_back((DWORD)(uintptr_t)currentAddr, 0);
                    threadMatchesFound++;
                    g_regionsScanned++;
                    g_bytesScanned += mbi.RegionSize;
                    threadBytesScanned += mbi.RegionSize;
                    continue;
                }
            }

            LOG_DEBUG("Thread %lu: Scanning region at 0x%p (Size: %zu bytes)", 
                     threadId, mbi.BaseAddress, mbi.RegionSize);

            while (remaining > 0 && !g_cancelScan) {
                SIZE_T bytesToRead = std::min(remaining, BUFFER_SIZE);
                SIZE_T actualRead = 0;

                if (!ReadProcessMemory(processHandle, currentAddr, buffer.data(), 
                                    bytesToRead, &actualRead) || actualRead == 0) {
                    break;
                }

                bool useVectorized = data->settings->useVectorizedOperations && 
                                   (currentValueType == VALUE_TYPE_INT);

                if (useVectorized) {
                    if (VectorizedValueMatch(buffer.data(), data->valueToFind, currentValueType, actualRead)) {
                        for (SIZE_T i = 0; i <= actualRead - valueTypeSize; i += sizeof(int)) {
                            if (ValueMatches(&buffer[i], data->valueToFind, currentValueType)) {
                                DWORD address = (DWORD)((uintptr_t)currentAddr + i);
                                
                                int confirmValue;
                                if (ReadProcessMemory(processHandle, (LPCVOID)(uintptr_t)address, 
                                    &confirmValue, sizeof(confirmValue), nullptr) && 
                                    confirmValue == data->valueToFind) {
                                    
                                    localResults.emplace_back(address, confirmValue);
                                    threadMatchesFound++;
                                    
                                    if (localResults.size() >= SCAN_BATCH_SIZE) {
                                        std::lock_guard<std::mutex> lock(scanResultsMutex);
                                        SaveBatchResults(data->results, localResults);
                                        localResults.clear();
                                    }
                                }
                            }
                        }
                    }
                } else {
                    int stride = 1;
                    if (currentValueType == VALUE_TYPE_INT) {
                        stride = 4;
                    } else if (currentValueType == VALUE_TYPE_AUTO) {
                        stride = 1;
                    }
                    
                    for (SIZE_T i = 0; i <= actualRead - valueTypeSize; i += stride) {
                        if (ValueMatches(&buffer[i], data->valueToFind, currentValueType)) {
                            DWORD address = (DWORD)((uintptr_t)currentAddr + i);
                            
                            int confirmValue;
                            if (ReadProcessMemory(processHandle, (LPCVOID)(uintptr_t)address, 
                                &confirmValue, sizeof(confirmValue), nullptr)) {
                                
                                if (currentValueType == VALUE_TYPE_INT && confirmValue == data->valueToFind) {
                                    localResults.emplace_back(address, confirmValue);
                                    threadMatchesFound++;
                                } else if (currentValueType != VALUE_TYPE_INT) {
                                    // For non-int types, do a second match check
                                    BYTE confirmBuffer[8];  // Large enough for any type
                                    if (ReadProcessMemory(processHandle, (LPCVOID)(uintptr_t)address,
                                        confirmBuffer, valueTypeSize, nullptr) && 
                                        ValueMatches(confirmBuffer, data->valueToFind, currentValueType)) {
                                        
                                        localResults.emplace_back(address, confirmValue);
                                        threadMatchesFound++;
                                    }
                                }
                                
                                if (localResults.size() >= SCAN_BATCH_SIZE) {
                                    std::lock_guard<std::mutex> lock(scanResultsMutex);
                                    SaveBatchResults(data->results, localResults);
                                    localResults.clear();
                                }
                            }
                        }
                    }
                }

                currentAddr += actualRead;
                remaining -= actualRead;
                g_bytesScanned += actualRead;
                threadBytesScanned += actualRead;
            }
            g_regionsScanned++;
        }

        if (!localResults.empty()) {
            std::lock_guard<std::mutex> lock(scanResultsMutex);
            SaveBatchResults(data->results, localResults);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Thread %lu error: %s", threadId, e.what());
    }
    
    CloseHandle(processHandle);
    return 0;
}

void SaveBatchResults(ScanResults* results, std::vector<std::pair<DWORD, int>>& batch) {
    std::lock_guard<std::mutex> lock(scanResultsMutex);
    
    LOG_DEBUG("Starting batch save of %zu results (Current total: %zu)", 
              batch.size(), results->count);
    
    size_t beforeCount = results->count;
    size_t added = 0;
    
    for (const auto& result : batch) {
        if (addScanResult(results, result.first, result.second)) {
            added++;
        }
    }
    
    LOG_DEBUG("Batch save complete: Added %zu/%zu results (New total: %zu)", 
              added, batch.size(), results->count);
    
    g_resultsUpdated = true;
}

void DisplayScanResults(ImGuiTableFlags flags) {
    if (ImGui::BeginTable("ScanResultsTable", 3, flags | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, 
                         ImVec2(0, 400))) {
        
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Current Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Original Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1,1,0,1), "Results: %zu/%zu", 
                          g_scanResults.count, g_settings.maxScanResults);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(1,1,0,1), "Memory: %.2f MB", 
                          (float)g_bytesScanned.load() / (1024.0f*1024.0f));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(1,1,0,1), "Matches: %zu", 
                          g_matchesFound.load());

        if (g_scanResults.count > 0 && g_scanResults.entries != nullptr) {
            static float lastClickTime = 0.0f;
            const float doubleClickTime = 0.3f;

            bool processHandleValid = g_currentProcess.processHandle && 
                                    g_currentProcess.processId != 0;

            for (size_t i = 0; i < g_scanResults.count; i++) {
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                char addressStr[32];
                snprintf(addressStr, sizeof(addressStr), "0x%08X", 
                        g_scanResults.entries[i].address);

                if (ImGui::Selectable(addressStr, false, ImGuiSelectableFlags_SpanAllColumns)) {
                    float currentTime = ImGui::GetTime();
                    if (currentTime - lastClickTime <= doubleClickTime) {
                        strcpy_s(g_addressInput, sizeof(g_addressInput), &addressStr[2]); // Skip the '0x' prefix
                        
                        if (processHandleValid) {
                            int currentValue = 0;
                            SIZE_T bytesRead = 0;
                            if (SafeReadMemoryWithRetry(g_currentProcess.processHandle, 
                                                (LPVOID)(uintptr_t)g_scanResults.entries[i].address,
                                                &currentValue, sizeof(currentValue), &bytesRead, 3)) {
                                newValue = currentValue;
                            } else {
                                newValue = g_scanResults.entries[i].value;
                            }
                        } else {
                            newValue = g_scanResults.entries[i].value;
                        }
                        
                        LOG_DEBUG("Double-clicked result - Address: %s, Value: %d", 
                                g_addressInput, newValue);
                        ShowStatusMessage("Value copied to memory modification");
                    }
                    lastClickTime = currentTime;
                }

                ImGui::TableSetColumnIndex(1);
                
                if (processHandleValid) {
                    int currentValue;
                    SIZE_T bytesRead = 0;
                    DWORD lastError = 0;
                    bool readResult = false;
                    
                    readResult = SafeReadMemoryWithRetry(
                        g_currentProcess.processHandle,
                        (LPVOID)(uintptr_t)g_scanResults.entries[i].address,
                        &currentValue,
                        sizeof(currentValue),
                        &bytesRead,
                        3
                    );
                    
                    if (!readResult) {
                        lastError = GetLastError();
                    }
                    
                    if (readResult && bytesRead == sizeof(currentValue)) {
                        bool valueChanged = (currentValue != g_scanResults.entries[i].originalValue);
                        
                        if (valueChanged) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%d", currentValue);
                        } else {
                            ImGui::Text("%d", currentValue);
                        }
                        
                        g_scanResults.entries[i].value = currentValue;
                    } 
                    else if (readResult && bytesRead > 0) {
                        ImGui::TextColored(ImVec4(1,0.5f,0,1), "%d (partial)", currentValue);
                        
                        LOG_DEBUG("Partial read at 0x%08X: got %zu of %zu bytes", 
                                 g_scanResults.entries[i].address, bytesRead, sizeof(currentValue));
                        g_scanResults.entries[i].value = currentValue;
                    }
                    else {
                        if (lastError == ERROR_PARTIAL_COPY) {
                            ImGui::TextColored(ImVec4(1,0.5f,0,1), "PARTIAL");
                        }
                        else if (lastError == ERROR_NOACCESS) {
                            ImGui::TextColored(ImVec4(1,0,0,1), "NO ACCESS");
                        }
                        else if (lastError == ERROR_INVALID_HANDLE) {
                            ImGui::TextColored(ImVec4(1,0,0,1), "INV HANDLE");
                        }
                        else if (lastError == ERROR_INVALID_PARAMETER) {
                            ImGui::TextColored(ImVec4(1,0,0,1), "INV PARAM");
                        }
                        else {
                            const char* errorStr = GetLastErrorAsString(lastError);
                            ImGui::TextColored(ImVec4(1,0,0,1), "ERR: %s", errorStr);
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "%d [cached]", 
                                    g_scanResults.entries[i].value);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", g_scanResults.entries[i].originalValue);
            }
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1,0.5f,0,1), 
                "No results found - Try scanning for a different value");
        }

        ImGui::EndTable();
        
        ImGui::Text("Regions scanned: %zu/%zu (%.1f%%)", 
                    g_regionsScanned.load(), g_totalRegionsToScan,
                    (g_totalRegionsToScan > 0) ? 
                    (100.0f * g_regionsScanned.load() / g_totalRegionsToScan) : 0.0f);
                    
        if (g_currentProcess.processId != 0 && 
            (g_currentProcess.processHandle == NULL || 
            GetProcessVersion(g_currentProcess.processId) == 0)) {
            
            ImGui::TextColored(ImVec4(1,0,0,1), "Process connection lost!");
            
            if (ImGui::Button("Reattach to Process")) {
                if (attachToProcess(&g_currentProcess, g_currentProcess.processId)) {
                    ShowStatusMessage("Successfully reattached to process");
                } else {
                    ShowStatusMessage("Failed to reattach to process");
                }
            }
        }
    }
}

void ShowScanProgressDialog() {
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scan Progress", nullptr, 
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        
        float scanProgress = (g_totalRegionsToScan > 0) ? 
            (float)g_regionsScanned.load() / (float)g_totalRegionsToScan : 0.0f;
        
        float memoryProgress = (g_totalMemoryToScan > 0) ?
            (float)g_bytesScanned.load() / (float)g_totalMemoryToScan : 0.0f;

        char overlayText[32];
        
        ImGui::Text("Regions Scanned: %zu/%zu", g_regionsScanned.load(), g_totalRegionsToScan);
        snprintf(overlayText, sizeof(overlayText), "%.1f%%", scanProgress * 100.0f);
        ImGui::ProgressBar(scanProgress, ImVec2(-1, 0), overlayText);
        
        ImGui::Text("Memory Scanned: %.2f MB / %.2f MB", 
                    g_bytesScanned.load() / (1024.0f * 1024.0f),
                    g_totalMemoryToScan / (1024.0f * 1024.0f));
        snprintf(overlayText, sizeof(overlayText), "%.1f%%", memoryProgress * 100.0f);
        ImGui::ProgressBar(memoryProgress, ImVec2(-1, 0), overlayText);

        ImGui::Separator();
        ImGui::Text("Matches Found: %zu", g_matchesFound.load());
        
        static float lastBytesScanned = 0;
        static float lastUpdateTime = ImGui::GetTime();
        float currentTime = ImGui::GetTime();
        float deltaTime = currentTime - lastUpdateTime;
        
        static float scanSpeed = 0.0f;
        if (deltaTime >= 0.5f) {
            float bytesDelta = g_bytesScanned.load() - lastBytesScanned;
            scanSpeed = (bytesDelta / deltaTime) / (1024.0f * 1024.0f);
            
            lastBytesScanned = g_bytesScanned.load();
            lastUpdateTime = currentTime;
        }
        
        ImGui::Text("Scan Speed: %.2f MB/s", scanSpeed);
        ImGui::Text("Regions Skipped: %zu", g_regionsSkipped.load());
        
        ImGui::Separator();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120) * 0.5f);
        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            g_cancelScan = true;
            LOG_INFO("Scan cancelled by user");
            ShowStatusMessage("Scan cancelled by user");
        }
        
        ImGui::End();
    }
}
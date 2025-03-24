#pragma once

#include "settings.h"
#include "include/imgui.h"

struct DebugInfo {
    char lastError[256];     // Last error message
    char uiState[256];       // Current UI state description
    int frameCount;          // Number of frames rendered
    bool isWelcomeGuideOpen; // Welcome guide state
    bool isSettingsOpen;     // Settings dialog state
    ImGuiContext* context;   // ImGui context pointer
    bool isDebugMode;        // Controls debug mode state
    char buildVersion[64];   // Stores the current build version
    bool isVerboseLogging;   // Controls verbose logging output
    bool isValid() const {
        return uiState[0] != '\0' && lastError != nullptr;
    }
};

extern DebugInfo g_debugInfo;
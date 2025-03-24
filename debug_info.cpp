#include "debug_info.h"

DebugInfo g_debugInfo = {
    "",               // lastError
    "",               // uiState
    0,                // frameCount
    false,            // isWelcomeGuideOpen
    false,            // isSettingsOpen
    nullptr,          // context
    false,            // isDebugMode
    "",               // buildVersion 
    false             // isVerboseLogging
};
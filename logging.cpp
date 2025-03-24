#include "logging.h"
#include "debug_info.h"
#include <windows.h>
#include <psapi.h>
#include <sys/stat.h>
#include <direct.h>
#include <time.h>
#include <sstream>

Logger::Logger() : logFile(nullptr), enableFileLogging(false), enableConsoleLogging(false), maxEntries(1000) {
}

Logger::~Logger() {
    if (logFile) {
        fclose(logFile);
        logFile = nullptr;
    }
}

void Logger::init(bool enableFileLogging, bool enableConsoleLogging) {
    this->enableFileLogging = enableFileLogging;
    this->enableConsoleLogging = enableConsoleLogging;

    if (enableFileLogging) {
        int mkdirResult = _mkdir("logs");
        
        if (mkdirResult == -1 && errno != EEXIST) {
            if (enableConsoleLogging) {
                printf("[ERROR] Failed to create logs directory. Error code: %d\n", errno);
            }
            
            logFile = fopen("CEngine_fallback.log", "w");
            if (logFile) {
                fprintf(logFile, "=== FALLBACK LOGGING ===\n");
                fprintf(logFile, "Failed to create logs directory. Error code: %d\n", errno);
            }
            return;
        }
        
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        
        char filename[256];
        snprintf(filename, sizeof(filename), "logs/CEngine_%s.log", timestamp);
        
        logFile = fopen(filename, "w");
        
        if (logFile) {
            fprintf(logFile, "=== CEngine Session Log ===\n");
            fprintf(logFile, "Session started: %s\n", asctime(&timeinfo));
            
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            
            fprintf(logFile, "\nSystem Information:\n");
            fprintf(logFile, "Windows Version: %lu\n", GetVersion());
            fprintf(logFile, "Processor Count: %lu\n", sysInfo.dwNumberOfProcessors);
            fprintf(logFile, "Page Size: %lu\n", sysInfo.dwPageSize);
            fprintf(logFile, "Total Physical Memory: %.2f GB\n", 
                    (float)memInfo.ullTotalPhys / (1024*1024*1024));
            fprintf(logFile, "Available Physical Memory: %.2f GB\n", 
                    (float)memInfo.ullAvailPhys / (1024*1024*1024));
            fprintf(logFile, "\n=== Session Log Begin ===\n\n");
            
            fflush(logFile);
        }
        else {
            if (enableConsoleLogging) {
                printf("[ERROR] Failed to create log file: %s. Error code: %d\n", 
                       filename, errno);
            }
            
            logFile = fopen("CEngine_fallback.log", "w");
            if (logFile) {
                fprintf(logFile, "=== FALLBACK LOGGING ===\n");
                fprintf(logFile, "Failed to create log in logs directory. Error code: %d\n", errno);
            }
        }
    }
    
    logEntries.clear();
}

void Logger::log(LogLevel level, const char* format, ...) {
    if (!enableFileLogging && !enableConsoleLogging)
        return;

    std::lock_guard<std::mutex> lock(logMutex);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

    const char* levelStr;
    switch (level) {
        case LOG_DEBUG:   levelStr = "DEBUG"; break;
        case LOG_INFO:    levelStr = "INFO"; break;
        case LOG_WARNING: levelStr = "WARN"; break;
        case LOG_ERROR:   levelStr = "ERROR"; break;
        case LOG_CRITICAL: levelStr = "CRIT"; break;
        case LOG_SECURITY: levelStr = "SECURITY"; break;
        default:          levelStr = "UNKNOWN"; break;
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    DWORD threadId = GetCurrentThreadId();

    const size_t maxMessageSize = 2048;
    char fullMessage[maxMessageSize];
    int result = snprintf(fullMessage, maxMessageSize, "[%s][%s][%lu] %s\n", 
             timestamp, levelStr, threadId, buffer);
    if (result < 0 || result >= maxMessageSize) {
        const char* truncMsg = "... (truncated)";
        size_t truncLen = strlen(truncMsg);
        if (maxMessageSize > truncLen + 1) {
            strcpy_s(&fullMessage[maxMessageSize - truncLen - 1], truncLen + 1, truncMsg);
        }
    }
    
    std::string logEntry(fullMessage);
    logEntries.push_back(logEntry);
    if (logEntries.size() > maxEntries) {
        logEntries.erase(logEntries.begin());
    }

    if (enableFileLogging && logFile) {
        if (level >= LOG_ERROR) {
            fprintf(logFile, "\n=== Error Details ===\n");
            fprintf(logFile, "Message: %s\n", buffer);
            
            if (g_debugInfo.isValid()) {
                fprintf(logFile, "\nDebug State:\n");
                fprintf(logFile, "UI State: %s\n", g_debugInfo.uiState);
                fprintf(logFile, "Frame Count: %d\n", g_debugInfo.frameCount);
                fprintf(logFile, "ImGui Context: %p\n", g_debugInfo.context);
                fprintf(logFile, "Last Error: %s\n", g_debugInfo.lastError);
            }
            
            PROCESS_MEMORY_COUNTERS_EX pmc;
            ZeroMemory(&pmc, sizeof(PROCESS_MEMORY_COUNTERS_EX));
            pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
            
            if (GetProcessMemoryInfo(GetCurrentProcess(), 
                (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                fprintf(logFile, "\nMemory Usage:\n");
                fprintf(logFile, "Working Set: %.2f MB\n", 
                    pmc.WorkingSetSize / (1024.0f * 1024.0f));
                fprintf(logFile, "Private Usage: %.2f MB\n", 
                    pmc.PrivateUsage / (1024.0f * 1024.0f));
            }
            
            fprintf(logFile, "\n=== End Error Details ===\n\n");
        }
        
        fputs(fullMessage, logFile);
        fflush(logFile);
    }

    if (enableConsoleLogging) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color;
        
        switch (level) {
            case LOG_DEBUG:    color = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
            case LOG_INFO:     color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
            case LOG_WARNING:  color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
            case LOG_ERROR:    color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
            case LOG_CRITICAL: color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
            case LOG_SECURITY: color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
            default:          color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
        }
        
        SetConsoleTextAttribute(hConsole, color);
        printf("%s", fullMessage);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

void Logger::clearLogs() {
    std::lock_guard<std::mutex> lock(logMutex);
    logEntries.clear();
    
    if (logFile) {
        fclose(logFile);
        std::string filename = getLogFilename();
        logFile = fopen(filename.c_str(), "w");
        
        if (logFile) {
            fprintf(logFile, "=========================================\n");
            fprintf(logFile, "CEngine Log Cleared: %s\n", getCurrentTimestamp().c_str());
            fprintf(logFile, "=========================================\n");
            fflush(logFile);
        }
    }
}

std::string Logger::getCurrentTimestamp() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    return timestamp;
}

const char* Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "ERROR";
        case LOG_CRITICAL: return "CRITICAL";
        case LOG_SECURITY: return "SECURITY";
        default:          return "UNKNOWN";
    }
}

std::string Logger::getLogFilename() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    
    char filename[256];
    strftime(filename, sizeof(filename), "logs/CEngine_%Y%m%d_%H%M%S.log", &timeinfo);
    
    return filename;
}

void Logger::logSecurityEvent(const char* format, ...) {
    std::lock_guard<std::mutex> lock(logMutex);
    
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    std::string timestamp = getCurrentTimestamp();
    
    char logLine[4096];
    snprintf(logLine, sizeof(logLine), "[%s] [SECURITY] %s", timestamp.c_str(), buffer);
    
    if (enableFileLogging && logFile) {
        fprintf(logFile, "%s\n", logLine);
        fflush(logFile);
    }
    
    logEntries.push_back(std::string("‼️ ") + logLine);
    if (logEntries.size() > maxEntries) {
        logEntries.erase(logEntries.begin());
    }
    
    if (enableConsoleLogging) {
        OutputDebugStringA(logLine);
        OutputDebugStringA("\n");
    }
}
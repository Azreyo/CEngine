#ifndef LOGGING_H
#define LOGGING_H

#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <mutex>


enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL,
    LOG_SECURITY
};

class Logger {
private:
    FILE* logFile;
    bool enableFileLogging;
    bool enableConsoleLogging;
    size_t maxEntries;
    std::vector<std::string> logEntries;
    std::mutex logMutex;
    
    std::string getCurrentTimestamp();
    std::string getLogFilename();
    const char* getLevelString(LogLevel level);

public:
    Logger();
    ~Logger();
    
    void init(bool enableFileLogging, bool enableConsoleLogging);
    void log(LogLevel level, const char* format, ...);
    void logSecurityEvent(const char* format, ...);
    void clearLogs();
    
    const std::vector<std::string>& getLogEntries() const { return logEntries; }

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
};

#define LOG_DEBUG(format, ...) Logger::getInstance().log(LOG_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Logger::getInstance().log(LOG_INFO, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) Logger::getInstance().log(LOG_WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Logger::getInstance().log(LOG_ERROR, format, ##__VA_ARGS__)
#define LOG_CRITICAL(format, ...) Logger::getInstance().log(LOG_CRITICAL, format, ##__VA_ARGS__)
#define LOG_SECURITY(format, ...) Logger::getInstance().logSecurityEvent(format, ##__VA_ARGS__)

#endif
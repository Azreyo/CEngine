#ifndef LOG_CONSOLE_H
#define LOG_CONSOLE_H

#include "include/imgui.h"
#include <string>
#include <vector>

class LogConsole {
private:
    bool isVisible;
    std::vector<std::string> items;
    bool autoScroll;
    bool scrollToBottom;
    ImGuiTextFilter filter;

public:
    LogConsole() : isVisible(false), autoScroll(true), scrollToBottom(false) {}
    void draw(const char* title, bool* p_open = nullptr);
    bool& getVisible() { return isVisible; }
    void setVisible(bool visible) { isVisible = visible; }
    void toggleVisible() { isVisible = !isVisible; }
    void clear();
};

#endif
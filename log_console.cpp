#include "log_console.h"
#include "logging.h"
#include "include/imgui.h"
#include <stdio.h>
#include <string>

void LogConsole::draw(const char* title, bool* p_open) {
    if (!ImGui::Begin(title, p_open, ImGuiWindowFlags_None)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginPopup("Options")) {
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        ImGui::EndPopup();
    }

    if (ImGui::Button("Options")) {
        ImGui::OpenPopup("Options");
    }
    ImGui::SameLine();
    bool clear = ImGui::Button("Clear");
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    
    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Filter:");
    ImGui::SameLine();
    filter.Draw("##filter", 180);
    
    ImGui::Separator();

    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    if (clear) {
        Logger::getInstance().clearLogs();
    }
    
    if (copy) {
        ImGui::LogToClipboard();
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    
    const auto& logEntries = Logger::getInstance().getLogEntries();
    
    for (const auto& entry : logEntries) {
        if (!filter.PassFilter(entry.c_str())) {
            continue;
        }
        
        ImVec4 color;
        bool hasColor = false;
        
        if (strstr(entry.c_str(), "[DEBUG]")) {
            color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            hasColor = true;
        }
        else if (strstr(entry.c_str(), "[INFO]")) {
            color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            hasColor = true;
        }
        else if (strstr(entry.c_str(), "[WARNING]")) {
            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            hasColor = true;
        }
        else if (strstr(entry.c_str(), "[ERROR]")) {
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            hasColor = true;
        }
        else if (strstr(entry.c_str(), "[CRITICAL]")) {
            color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            hasColor = true;
        }
        
        if (hasColor) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
        }
        
        ImGui::TextUnformatted(entry.c_str());
        
        if (hasColor) {
            ImGui::PopStyleColor();
        }
    }
    
    if (scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
        ImGui::SetScrollHereY(1.0f);
    }
    scrollToBottom = false;
    
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
}

void LogConsole::clear() {
    Logger::getInstance().clearLogs();
}
#pragma once

#include <string>

enum class ProxyMode { System = 0, Direct = 1, Manual = 2 };

struct CopilotUsageConfig {
    std::wstring token;
    std::wstring authPath;
    ProxyMode proxyMode = ProxyMode::Direct;
    std::wstring proxyServer;
    int refreshInterval = 60;
};

void SetConfigDirectory(const std::wstring& dir);
std::wstring ResolveCopilotConfigPath();
CopilotUsageConfig LoadCopilotConfig(const std::wstring& path = ResolveCopilotConfigPath());
bool SaveCopilotConfig(const CopilotUsageConfig& config, std::wstring* error = nullptr,
                      const std::wstring& path = ResolveCopilotConfigPath());
bool NormalizeCopilotConfig(CopilotUsageConfig* config, std::wstring* error = nullptr);

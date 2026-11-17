#pragma once

#include "CopilotUsageConfig.h"
#include "CopilotUsageCore.h"

#include <optional>
#include <string>

class CopilotUsageFetcher {
public:
    CopilotUsageSnapshot Fetch(const CopilotUsageConfig& config = {}) const;

    // Helper exposed for testing token discovery
    std::optional<std::string> DiscoverToken(const CopilotUsageConfig& config, std::wstring* errorMessage = nullptr) const;

private:
    std::optional<std::string> ReadTokenFromFile(const std::wstring& path, std::wstring* errorMessage) const;
    std::optional<std::string> ReadTokenFromCredManager() const;
    std::optional<std::string> ReadTokenFromEnv() const;
    std::optional<std::string> ReadTokenFromGhCli() const;
    std::optional<std::string> LoadFileUtf8(const std::wstring& path, std::wstring* errorMessage) const;
    std::optional<std::string> ExtractTokenFromJsonOrText(const std::string& content) const;

    std::optional<std::string> HttpGetJson(
        const std::wstring& host,
        const std::wstring& path,
        const std::string& token,
        const CopilotUsageConfig& config,
        std::wstring* errorMessage) const;
};

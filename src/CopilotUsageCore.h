#pragma once

#include <optional>
#include <string>
#include <vector>

struct QuotaBucket {
    bool present = false;
    bool unlimited = false;
    double percentRemaining = 0.0;
    long long remaining = 0;
    long long entitlement = 0;
    long long creditsUsed = 0;
};

struct CopilotUsageSnapshot {
    bool success = false;
    std::wstring login;
    std::wstring plan;
    std::wstring resetDate;
    std::wstring errorMessage;
    QuotaBucket premium;
    QuotaBucket chat;
    QuotaBucket completions;
};

std::wstring Utf8ToWide(const std::string& input);
std::string WideToUtf8(const std::wstring& input);

CopilotUsageSnapshot ParseCopilotUsageJson(const std::string& jsonText, std::wstring* errorMessage = nullptr);

std::wstring FormatBucketValue(const QuotaBucket& bucket);
float GetBucketUsageRatio(const QuotaBucket& bucket);

std::wstring BuildCopilotTooltip(const CopilotUsageSnapshot& snapshot);

#include "CopilotUsageConfig.h"

#include <Windows.h>
#include <winhttp.h>
#include <cwctype>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::wstring s_configDirectory;

std::wstring Trim(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

std::wstring ReadValue(const std::wstring& path, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"CopilotUsage", key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

int ReadIntValue(const std::wstring& path, const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(L"CopilotUsage", key, fallback, path.c_str());
}

}  // namespace

void SetConfigDirectory(const std::wstring& dir) {
    s_configDirectory = dir;
    if (!s_configDirectory.empty() && s_configDirectory.back() != L'\\' && s_configDirectory.back() != L'/') {
        s_configDirectory += L'\\';
    }
}

std::wstring ResolveCopilotConfigPath() {
    if (!s_configDirectory.empty()) {
        std::wstring candidate = s_configDirectory + L"CopilotUsage.ini";
        return candidate;
    }

    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), buffer.data(), static_cast<DWORD>(buffer.size()));
    const std::wstring module(buffer.data(), length);
    return module.substr(0, module.find_last_of(L"\\/") + 1) + L"CopilotUsage.ini";
}

CopilotUsageConfig LoadCopilotConfig(const std::wstring& path) {
    CopilotUsageConfig config;
    config.token = ReadValue(path, L"token");
    config.authPath = ReadValue(path, L"auth_path");
    config.proxyServer = ReadValue(path, L"proxy_server");
    config.refreshInterval = ReadIntValue(path, L"refresh_interval", 60);
    if (config.refreshInterval < 15) config.refreshInterval = 15;

    const auto mode = ReadValue(path, L"proxy_mode", L"direct");
    if (mode == L"system") config.proxyMode = ProxyMode::System;
    else if (mode == L"manual") config.proxyMode = ProxyMode::Manual;
    else config.proxyMode = ProxyMode::Direct;

    return config;
}

bool NormalizeCopilotConfig(CopilotUsageConfig* config, std::wstring* error) {
    if (error) error->clear();
    if (config->proxyMode != ProxyMode::System && config->proxyMode != ProxyMode::Direct && config->proxyMode != ProxyMode::Manual) {
        if (error) *error = L"Please select a valid proxy mode.";
        return false;
    }

    config->token = Trim(config->token);
    config->authPath = Trim(config->authPath);
    config->proxyServer = Trim(config->proxyServer);

    if (config->token.find_first_of(L"\r\n") != std::wstring::npos ||
        config->authPath.find_first_of(L"\r\n") != std::wstring::npos ||
        config->proxyServer.find_first_of(L"\r\n") != std::wstring::npos) {
        if (error) *error = L"Configuration fields cannot contain newlines.";
        return false;
    }

    if (config->refreshInterval < 15) config->refreshInterval = 15;

    if (config->proxyMode != ProxyMode::Manual) return true;

    std::wstring url = config->proxyServer;
    if (url.find(L"://") == std::wstring::npos) url = L"http://" + url;

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength =
        parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTP ||
        !parts.dwHostNameLength || !parts.nPort || parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwExtraInfoLength || (parts.dwUrlPathLength && std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) != L"/")) {
        if (error) *error = L"Please enter a valid HTTP proxy address (e.g. 127.0.0.1:7890).";
        return false;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    for (wchar_t ch : host) {
        if (iswspace(ch) || ch == L';' || ch == L'=') {
            if (error) *error = L"Proxy host name contains invalid characters.";
            return false;
        }
    }

    if (host.find(L':') != std::wstring::npos && host.front() != L'[') host = L"[" + host + L"]";
    config->proxyServer = host + L":" + std::to_wstring(parts.nPort);
    return true;
}

bool SaveCopilotConfig(const CopilotUsageConfig& input, std::wstring* error, const std::wstring& path) {
    auto config = input;
    if (!NormalizeCopilotConfig(&config, error)) return false;

    const wchar_t* mode = config.proxyMode == ProxyMode::Manual ? L"manual" :
        config.proxyMode == ProxyMode::System ? L"system" : L"direct";

    std::wstring text = L"\ufeff[CopilotUsage]\r\n";
    text += L"token=" + config.token + L"\r\n";
    text += L"auth_path=" + config.authPath + L"\r\n";
    text += L"proxy_mode=" + std::wstring(mode) + L"\r\n";
    text += L"proxy_server=" + config.proxyServer + L"\r\n";
    text += L"refresh_interval=" + std::to_wstring(config.refreshInterval) + L"\r\n";

    const auto temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = L"Failed to save configuration. Check write permissions: " + path;
        return false;
    }

    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    const bool saved = WriteFile(file, text.data(), size, &written, nullptr) && written == size;
    CloseHandle(file);

    if (!saved || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        if (error) *error = L"Configuration write failed: " + path;
        return false;
    }

    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}

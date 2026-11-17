#include "CopilotUsageFetcher.h"
#include "JsonLite.h"

#include <Windows.h>
#include <wincred.h>
#include <winhttp.h>
#include <shlwapi.h>

#include <array>
#include <regex>
#include <vector>

namespace {

std::wstring GetEnvVar(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::vector<wchar_t> buffer(size);
    DWORD length = GetEnvironmentVariableW(name, buffer.data(), size);
    return length && length < size ? std::wstring(buffer.data(), length) : L"";
}

std::string TrimAscii(const std::string& str) {
    const auto first = str.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : str.substr(first, str.find_last_not_of(" \t\r\n") - first + 1);
}

bool IsValidTokenCandidate(const std::string& token) {
    if (token.empty() || token.length() < 10) return false;
    for (char c : token) {
        if (c <= 32 || c >= 127) return false;
    }
    return true;
}

}  // namespace

CopilotUsageSnapshot CopilotUsageFetcher::Fetch(const CopilotUsageConfig& input) const {
    auto config = input;
    CopilotUsageSnapshot snapshot;

    std::wstring normalizeError;
    if (!NormalizeCopilotConfig(&config, &normalizeError)) {
        snapshot.errorMessage = normalizeError;
        return snapshot;
    }

    std::wstring tokenError;
    auto token = DiscoverToken(config, &tokenError);
    if (!token.has_value()) {
        snapshot.errorMessage = tokenError.empty() ? L"GitHub Copilot token not found" : tokenError;
        return snapshot;
    }

    std::wstring httpError;
    auto jsonBody = HttpGetJson(L"api.github.com", L"/copilot_internal/user", *token, config, &httpError);
    if (!jsonBody.has_value()) {
        snapshot.errorMessage = httpError;
        return snapshot;
    }

    snapshot = ParseCopilotUsageJson(*jsonBody, &httpError);
    if (!snapshot.success) {
        snapshot.errorMessage = httpError;
    }
    return snapshot;
}

std::optional<std::string> CopilotUsageFetcher::DiscoverToken(const CopilotUsageConfig& config, std::wstring* errorMessage) const {
    if (!config.token.empty()) {
        std::string tok = WideToUtf8(config.token);
        if (IsValidTokenCandidate(tok)) return tok;
    }

    if (!config.authPath.empty()) {
        return ReadTokenFromFile(config.authPath, errorMessage);
    }

    // 1. Check Windows Credential Manager
    if (auto credToken = ReadTokenFromCredManager(); credToken.has_value()) {
        return credToken;
    }

    // 2. Check standard Copilot JSON files in LocalAppData and UserProfile
    std::vector<std::wstring> candidateFiles;
    auto localAppData = GetEnvVar(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        candidateFiles.push_back(localAppData + L"\\github-copilot\\hosts.json");
        candidateFiles.push_back(localAppData + L"\\github-copilot\\apps.json");
    }
    auto userProfile = GetEnvVar(L"USERPROFILE");
    if (!userProfile.empty()) {
        candidateFiles.push_back(userProfile + L"\\.config\\github-copilot\\hosts.json");
        candidateFiles.push_back(userProfile + L"\\.config\\github-copilot\\apps.json");
    }

    for (const auto& path : candidateFiles) {
        if (PathFileExistsW(path.c_str())) {
            if (auto t = ReadTokenFromFile(path, nullptr); t.has_value()) {
                return t;
            }
        }
    }

    // 3. Check environment variables
    if (auto envToken = ReadTokenFromEnv(); envToken.has_value()) {
        return envToken;
    }

    // 4. Fallback: try gh auth token via CLI
    if (auto ghToken = ReadTokenFromGhCli(); ghToken.has_value()) {
        return ghToken;
    }

    if (errorMessage) {
        *errorMessage = L"No GitHub token found. Please sign in via 'gh auth login' or specify token in options.";
    }
    return std::nullopt;
}

std::optional<std::string> CopilotUsageFetcher::ReadTokenFromCredManager() const {
    auto tryTarget = [](const wchar_t* target) -> std::optional<std::string> {
        PCREDENTIALW cred = nullptr;
        if (CredReadW(target, CRED_TYPE_GENERIC, 0, &cred)) {
            std::string tok;
            if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
                tok = TrimAscii(std::string(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize));
            }
            CredFree(cred);
            if (IsValidTokenCandidate(tok)) {
                return tok;
            }
        }
        return std::nullopt;
    };

    if (auto t = tryTarget(L"gh:github.com:"); t.has_value()) return t;

    DWORD count = 0;
    PCREDENTIALW* pCreds = nullptr;
    if (CredEnumerateW(L"gh:github.com*", 0, &count, &pCreds) && pCreds) {
        for (DWORD i = 0; i < count; ++i) {
            if (pCreds[i]->CredentialBlob && pCreds[i]->CredentialBlobSize > 0) {
                std::string tok = TrimAscii(std::string(reinterpret_cast<const char*>(pCreds[i]->CredentialBlob), pCreds[i]->CredentialBlobSize));
                if (IsValidTokenCandidate(tok)) {
                    CredFree(pCreds);
                    return tok;
                }
            }
        }
        CredFree(pCreds);
    }

    count = 0;
    pCreds = nullptr;
    if (CredEnumerateW(L"copilot-cli*", 0, &count, &pCreds) && pCreds) {
        for (DWORD i = 0; i < count; ++i) {
            if (pCreds[i]->CredentialBlob && pCreds[i]->CredentialBlobSize > 0) {
                std::string tok = TrimAscii(std::string(reinterpret_cast<const char*>(pCreds[i]->CredentialBlob), pCreds[i]->CredentialBlobSize));
                if (IsValidTokenCandidate(tok)) {
                    CredFree(pCreds);
                    return tok;
                }
            }
        }
        CredFree(pCreds);
    }

    return std::nullopt;
}

std::optional<std::string> CopilotUsageFetcher::ReadTokenFromEnv() const {
    for (const auto* name : { L"COPILOT_TOKEN", L"GH_TOKEN", L"GITHUB_TOKEN" }) {
        std::wstring val = GetEnvVar(name);
        if (!val.empty()) {
            std::string tok = TrimAscii(WideToUtf8(val));
            if (IsValidTokenCandidate(tok)) return tok;
        }
    }
    return std::nullopt;
}

std::optional<std::string> CopilotUsageFetcher::ReadTokenFromGhCli() const {
    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return std::nullopt;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"gh auth token";
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return std::nullopt;
    }
    CloseHandle(hWritePipe);

    std::string output;
    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, bytesRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode == 0) {
        std::string tok = TrimAscii(output);
        if (IsValidTokenCandidate(tok)) return tok;
    }
    return std::nullopt;
}

std::optional<std::string> CopilotUsageFetcher::ReadTokenFromFile(const std::wstring& path, std::wstring* errorMessage) const {
    auto content = LoadFileUtf8(path, errorMessage);
    if (!content.has_value()) return std::nullopt;

    auto tok = ExtractTokenFromJsonOrText(*content);
    if (!tok.has_value()) {
        if (errorMessage) {
            *errorMessage = L"No valid token found in " + path;
        }
        return std::nullopt;
    }
    return tok;
}

std::optional<std::string> CopilotUsageFetcher::ExtractTokenFromJsonOrText(const std::string& content) const {
    jsonlite::Parser parser(content);
    if (auto root = parser.Parse(); root.has_value() && root->IsObject()) {
        auto checkObj = [](const jsonlite::Value* obj) -> std::optional<std::string> {
            if (!obj || !obj->IsObject()) return std::nullopt;
            for (const char* key : { "oauth_token", "access_token", "token" }) {
                if (auto val = obj->Find(key); val != nullptr) {
                    if (auto str = val->AsString(); str.has_value()) {
                        std::string s(str->data(), str->size());
                        if (IsValidTokenCandidate(s)) return s;
                    }
                }
            }
            return std::nullopt;
        };

        if (auto t = checkObj(&(*root)); t.has_value()) return t;

        if (auto gh = root->Find("github.com"); gh != nullptr) {
            if (auto t = checkObj(gh); t.has_value()) return t;
        }
    }

    std::smatch m;
    if (std::regex_search(content, m, std::regex(R"delim("(?:oauth_token|access_token|token)"\s*:\s*"([^"]+)")delim"))) {
        std::string tok = m[1].str();
        if (IsValidTokenCandidate(tok)) return tok;
    }

    if (std::regex_search(content, m, std::regex(R"delim((?:^|\s)oauth_token:\s*([^\s]+))delim"))) {
        std::string tok = m[1].str();
        if (IsValidTokenCandidate(tok)) return tok;
    }

    if (std::regex_search(content, m, std::regex(R"delim((gh[oups]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}))delim"))) {
        std::string tok = m[1].str();
        if (IsValidTokenCandidate(tok)) return tok;
    }

    return std::nullopt;
}

std::optional<std::string> CopilotUsageFetcher::LoadFileUtf8(const std::wstring& path, std::wstring* errorMessage) const {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (errorMessage) *errorMessage = L"Cannot open file: " + path;
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        if (errorMessage) *errorMessage = L"File too large or unreadable: " + path;
        return std::nullopt;
    }

    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = contents.empty() || ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr);
    CloseHandle(file);

    if (!ok || read != contents.size()) {
        if (errorMessage) *errorMessage = L"Failed reading file: " + path;
        return std::nullopt;
    }
    return contents;
}

std::optional<std::string> CopilotUsageFetcher::HttpGetJson(
    const std::wstring& host,
    const std::wstring& path,
    const std::string& token,
    const CopilotUsageConfig& config,
    std::wstring* errorMessage) const {

    if (errorMessage) errorMessage->clear();

    DWORD accessType = config.proxyMode == ProxyMode::Manual ? WINHTTP_ACCESS_TYPE_NAMED_PROXY :
        config.proxyMode == ProxyMode::Direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;

    LPCWSTR proxyName = (config.proxyMode == ProxyMode::Manual && !config.proxyServer.empty())
        ? config.proxyServer.c_str() : WINHTTP_NO_PROXY_NAME;

    HINTERNET session = WinHttpOpen(L"TrafficMonitor-CopilotUsage/1.0", accessType, proxyName, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (errorMessage) *errorMessage = L"WinHttpOpen failed (" + std::to_wstring(GetLastError()) + L")";
        return std::nullopt;
    }

    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    std::optional<std::string> responseBody;

    do {
        connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            if (errorMessage) *errorMessage = L"WinHttpConnect failed (" + std::to_wstring(GetLastError()) + L")";
            break;
        }

        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            if (errorMessage) *errorMessage = L"WinHttpOpenRequest failed (" + std::to_wstring(GetLastError()) + L")";
            break;
        }

        std::vector<std::wstring> headers = {
            L"Authorization: token " + Utf8ToWide(token),
            L"Accept: application/json",
            L"Editor-Version: vscode/1.98.0",
            L"User-Agent: GitHubCopilotChat/0.25.0"
        };

        for (const auto& h : headers) {
            WinHttpAddRequestHeaders(request, h.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
        }

        constexpr DWORD timeout = 12000;
        WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            if (errorMessage) *errorMessage = L"WinHttpSendRequest failed (" + std::to_wstring(GetLastError()) + L")";
            break;
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            if (errorMessage) *errorMessage = L"WinHttpReceiveResponse failed (" + std::to_wstring(GetLastError()) + L")";
            break;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode != 200) {
            if (errorMessage) {
                *errorMessage = L"HTTP " + std::to_wstring(statusCode);
                if (statusCode == 401) {
                    *errorMessage += L": Unauthorized (token may be expired or missing Copilot scope)";
                } else if (statusCode == 403) {
                    *errorMessage += L": Forbidden (no Copilot subscription or rate limit)";
                }
            }
            break;
        }

        std::string body;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                if (errorMessage) *errorMessage = L"WinHttpQueryDataAvailable failed";
                break;
            }
            if (available == 0) {
                responseBody = std::move(body);
                break;
            }

            std::string chunk(available, '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &downloaded)) {
                if (errorMessage) *errorMessage = L"WinHttpReadData failed";
                break;
            }
            chunk.resize(downloaded);
            body.append(chunk);
        }
    } while (false);

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return responseBody;
}

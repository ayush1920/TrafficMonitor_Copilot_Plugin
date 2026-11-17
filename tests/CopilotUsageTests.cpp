#include "CopilotUsageConfig.h"
#include "CopilotUsageCore.h"
#include "CopilotUsageFetcher.h"

#include <Windows.h>
#include <cassert>
#include <iostream>
#include <string>

void TestParseCopilotUsageJson_LiveFormat() {
    const std::string sample = R"({
        "id": 159529953,
        "login": "pakumaro_amadeus",
        "copilot_plan": "business",
        "quota_reset_date": "2026-12-01",
        "quota_snapshots": {
            "chat": {
                "percent_remaining": 100.0,
                "unlimited": true,
                "remaining": 0,
                "entitlement": 0
            },
            "completions": {
                "percent_remaining": 100.0,
                "unlimited": true,
                "remaining": 0,
                "entitlement": 0
            },
            "premium_interactions": {
                "percent_remaining": 48.7,
                "quota_remaining": 14620.9,
                "unlimited": false,
                "credits_used": 15316,
                "remaining": 14620,
                "entitlement": 30000
            }
        }
    })";

    std::wstring err;
    CopilotUsageSnapshot snap = ParseCopilotUsageJson(sample, &err);
    assert(snap.success);
    assert(err.empty());
    assert(snap.login == L"pakumaro_amadeus");
    assert(snap.plan == L"business");
    assert(snap.resetDate == L"2026-12-01");

    assert(snap.premium.present);
    assert(!snap.premium.unlimited);
    assert(snap.premium.remaining == 14620);
    assert(snap.premium.entitlement == 30000);
    assert(snap.premium.percentRemaining > 48.6 && snap.premium.percentRemaining < 48.8);

    assert(snap.chat.present);
    assert(snap.chat.unlimited);
    assert(snap.chat.percentRemaining == 100.0);

    assert(snap.completions.present);
    assert(snap.completions.unlimited);

    std::wstring premVal = FormatBucketValue(snap.premium);
    assert(premVal == L"48.7%");

    std::wstring premValNoPct = FormatBucketValue(snap.premium, true);
    assert(premValNoPct == L"48.7");

    std::wstring chatVal = FormatBucketValue(snap.chat);
    assert(chatVal == L"Unlim");

    float ratio = GetBucketUsageRatio(snap.premium);
    assert(ratio > 0.48f && ratio < 0.49f);

    std::wstring tooltip = BuildCopilotTooltip(snap);
    assert(tooltip.find(L"pakumaro_amadeus") != std::wstring::npos);
    assert(tooltip.find(L"business") != std::wstring::npos);
    assert(tooltip.find(L"14,620 / 30,000") != std::wstring::npos);
    assert(tooltip.find(L"Unlimited") != std::wstring::npos);

    std::cout << "[PASS] TestParseCopilotUsageJson_LiveFormat" << std::endl;
}

void TestParseCopilotUsageJson_EdgeCases() {
    std::wstring err;
    auto snap1 = ParseCopilotUsageJson("not json", &err);
    assert(!snap1.success);
    assert(!err.empty());

    auto snap2 = ParseCopilotUsageJson("{}", &err);
    assert(!snap2.success);
    assert(!err.empty());

    // Top-level fallback format
    const std::string sample2 = R"({
        "copilot_plan": "individual",
        "premium_interactions": {
            "percent_remaining": 95.0,
            "unlimited": false,
            "remaining": 285,
            "entitlement": 300
        }
    })";
    auto snap3 = ParseCopilotUsageJson(sample2, &err);
    assert(snap3.success);
    assert(snap3.premium.present);
    assert(FormatBucketValue(snap3.premium) == L"95.0%");

    std::cout << "[PASS] TestParseCopilotUsageJson_EdgeCases" << std::endl;
}

void TestConfigSaveAndLoad() {
    CopilotUsageConfig cfg;
    cfg.token = L"gho_test1234567890";
    cfg.authPath = L"C:\\test\\hosts.json";
    cfg.proxyMode = ProxyMode::Manual;
    cfg.proxyServer = L"127.0.0.1:7890";
    cfg.refreshInterval = 45;

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring iniFile = std::wstring(tempPath) + L"CopilotUsageTest_" + std::to_wstring(GetTickCount64()) + L".ini";

    std::wstring err;
    bool ok = SaveCopilotConfig(cfg, &err, iniFile);
    assert(ok);
    assert(err.empty());

    CopilotUsageConfig loaded = LoadCopilotConfig(iniFile);
    assert(loaded.token == L"gho_test1234567890");
    assert(loaded.authPath == L"C:\\test\\hosts.json");
    assert(loaded.proxyMode == ProxyMode::Manual);
    assert(loaded.proxyServer == L"127.0.0.1:7890");
    assert(loaded.refreshInterval == 45);

    DeleteFileW(iniFile.c_str());
    std::cout << "[PASS] TestConfigSaveAndLoad" << std::endl;
}

void TestTokenDiscovery() {
    CopilotUsageFetcher fetcher;
    CopilotUsageConfig cfg; // Default config: auto-detect

    std::wstring err;
    auto tok = fetcher.DiscoverToken(cfg, &err);
    assert(tok.has_value());
    assert(tok->length() >= 10);
    assert(tok->rfind("gh", 0) == 0); // starts with "gh" (e.g. gho_...)

    std::cout << "[PASS] TestTokenDiscovery (found token from Credential Manager: "
              << tok->substr(0, 4) << "... len: " << tok->length() << ")" << std::endl;
}

void TestLiveFetch() {
    CopilotUsageFetcher fetcher;
    CopilotUsageConfig cfg;
    CopilotUsageSnapshot snap = fetcher.Fetch(cfg);

    std::wcout << L"Live fetch result: success=" << (snap.success ? L"true" : L"false") << std::endl;
    if (snap.success) {
        std::wcout << L"  Account: " << snap.login << std::endl;
        std::wcout << L"  Plan: " << snap.plan << std::endl;
        std::wcout << L"  Reset Date: " << snap.resetDate << std::endl;
        std::wcout << L"  Premium Remaining: " << FormatBucketValue(snap.premium) << L" (" << snap.premium.remaining << L"/" << snap.premium.entitlement << L")" << std::endl;
        std::wcout << L"  Chat: " << FormatBucketValue(snap.chat) << std::endl;
        std::wcout << L"  Completions: " << FormatBucketValue(snap.completions) << std::endl;
        std::wcout << L"  Tooltip Preview:\n" << BuildCopilotTooltip(snap) << std::endl;
    } else {
        std::wcout << L"  Error: " << snap.errorMessage << std::endl;
    }
    assert(snap.success);
    std::cout << "[PASS] TestLiveFetch" << std::endl;
}

int main() {
    std::cout << "Running CopilotUsage tests..." << std::endl;
    TestParseCopilotUsageJson_LiveFormat();
    TestParseCopilotUsageJson_EdgeCases();
    TestConfigSaveAndLoad();
    TestTokenDiscovery();
    TestLiveFetch();
    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}

#include "CopilotUsageCore.h"
#include "JsonLite.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::wstring FormatNumberWithCommas(long long value) {
    std::wstring s = std::to_wstring(value);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(n, L",");
        n -= 3;
    }
    return s;
}

QuotaBucket ExtractBucket(const jsonlite::Value* node) {
    QuotaBucket bucket;
    if (node == nullptr || !node->IsObject()) {
        return bucket;
    }

    bucket.present = true;

    if (auto unl = node->Find("unlimited"); unl != nullptr) {
        if (auto b = unl->AsBool(); b.has_value()) {
            bucket.unlimited = *b;
        }
    }

    if (auto rem = node->Find("remaining"); rem != nullptr) {
        if (auto num = rem->AsNumber(); num.has_value()) {
            bucket.remaining = static_cast<long long>(*num);
        }
    } else if (auto qrem = node->Find("quota_remaining"); qrem != nullptr) {
        if (auto num = qrem->AsNumber(); num.has_value()) {
            bucket.remaining = static_cast<long long>(*num);
        }
    }

    if (auto ent = node->Find("entitlement"); ent != nullptr) {
        if (auto num = ent->AsNumber(); num.has_value()) {
            bucket.entitlement = static_cast<long long>(*num);
        }
    }

    if (auto cred = node->Find("credits_used"); cred != nullptr) {
        if (auto num = cred->AsNumber(); num.has_value()) {
            bucket.creditsUsed = static_cast<long long>(*num);
        }
    }

    if (auto pct = node->Find("percent_remaining"); pct != nullptr) {
        if (auto num = pct->AsNumber(); num.has_value()) {
            bucket.percentRemaining = *num;
        }
    } else if (bucket.entitlement > 0) {
        bucket.percentRemaining = (static_cast<double>(bucket.remaining) / bucket.entitlement) * 100.0;
    } else if (bucket.unlimited) {
        bucket.percentRemaining = 100.0;
    }

    bucket.percentRemaining = std::clamp(bucket.percentRemaining, 0.0, 100.0);
    return bucket;
}

}  // namespace

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size, nullptr, nullptr);
    return output;
}

CopilotUsageSnapshot ParseCopilotUsageJson(const std::string& jsonText, std::wstring* errorMessage) {
    if (errorMessage) errorMessage->clear();

    CopilotUsageSnapshot snapshot;
    jsonlite::Parser parser(jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage) {
            *errorMessage = L"Copilot JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return snapshot;
    }

    if (auto login = root->Find("login"); login != nullptr) {
        if (auto str = login->AsString(); str.has_value()) {
            snapshot.login = Utf8ToWide(std::string(*str));
        }
    }

    if (auto plan = root->Find("copilot_plan"); plan != nullptr) {
        if (auto str = plan->AsString(); str.has_value()) {
            snapshot.plan = Utf8ToWide(std::string(*str));
        }
    }

    if (auto reset = root->Find("quota_reset_date"); reset != nullptr) {
        if (auto str = reset->AsString(); str.has_value()) {
            snapshot.resetDate = Utf8ToWide(std::string(*str));
        }
    }

    const jsonlite::Value* snapshots = root->Find("quota_snapshots");
    const jsonlite::Value* container = (snapshots != nullptr && snapshots->IsObject()) ? snapshots : &(*root);

    snapshot.premium = ExtractBucket(container->Find("premium_interactions"));
    snapshot.chat = ExtractBucket(container->Find("chat"));
    snapshot.completions = ExtractBucket(container->Find("completions"));

    if (!snapshot.premium.present && !snapshot.chat.present && !snapshot.completions.present) {
        if (errorMessage) {
            *errorMessage = L"No quota buckets found in response";
        }
        return snapshot;
    }

    snapshot.success = true;
    return snapshot;
}

std::wstring FormatBucketValue(const QuotaBucket& bucket) {
    if (!bucket.present) return L"--";
    if (bucket.unlimited) return L"Unlim";

    std::wostringstream ss;
    if (bucket.percentRemaining >= 99.95) {
        ss << L"100%";
    } else if (bucket.percentRemaining <= 0.05) {
        ss << L"0%";
    } else {
        ss << std::fixed << std::setprecision(1) << bucket.percentRemaining << L"%";
    }
    return ss.str();
}

float GetBucketUsageRatio(const QuotaBucket& bucket) {
    if (!bucket.present) return 0.0f;
    if (bucket.unlimited) return 1.0f;
    return static_cast<float>(std::clamp(bucket.percentRemaining / 100.0, 0.0, 1.0));
}

std::wstring BuildCopilotTooltip(const CopilotUsageSnapshot& snapshot) {
    std::wostringstream ss;
    ss << L"GitHub Copilot Usage";
    if (!snapshot.plan.empty()) {
        ss << L" (" << snapshot.plan << L")";
    }

    if (!snapshot.success) {
        ss << L"\nError: " << (snapshot.errorMessage.empty() ? L"Waiting for data..." : snapshot.errorMessage);
        return ss.str();
    }

    if (!snapshot.login.empty()) {
        ss << L"\nAccount: " << snapshot.login;
    }

    auto appendBucket = [&](const wchar_t* label, const QuotaBucket& b) {
        if (!b.present) return;
        ss << L"\n" << label << L": ";
        if (b.unlimited) {
            ss << L"Unlimited";
        } else {
            ss << std::fixed << std::setprecision(1) << b.percentRemaining << L"% ("
               << FormatNumberWithCommas(b.remaining) << L" / "
               << FormatNumberWithCommas(b.entitlement) << L")";
        }
    };

    appendBucket(L"Premium Requests", snapshot.premium);
    appendBucket(L"Chat", snapshot.chat);
    appendBucket(L"Completions", snapshot.completions);

    if (!snapshot.resetDate.empty()) {
        ss << L"\nReset Date: " << snapshot.resetDate;
    }

    return ss.str();
}

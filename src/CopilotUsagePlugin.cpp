#include "CopilotUsageConfig.h"
#include "CopilotUsageCore.h"
#include "CopilotUsageFetcher.h"
#include "CopilotUsageOptions.h"
#include "CopilotUsageVersion.h"
#include "PluginInterface.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace {

class CopilotDisplayItem final : public IPluginItem {
public:
    enum class Kind { Premium, Chat, Completions };

    CopilotDisplayItem(Kind kind, const wchar_t* name, const wchar_t* id, const wchar_t* label, const wchar_t* sample)
        : kind_(kind), name_(name), id_(id), label_(label), sample_(sample) {}

    const wchar_t* GetItemName() const override { return name_; }
    const wchar_t* GetItemId() const override { return id_; }
    const wchar_t* GetItemLableText() const override { return label_; }
    const wchar_t* GetItemValueText() const override { return valueText_.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return sample_; }
    int IsDrawResourceUsageGraph() const override { return 1; }
    float GetResourceUsageGraphValue() const override { return usageRatio_; }

    void SetSnapshot(const CopilotUsageSnapshot& snapshot, bool hidePercent = false) {
        if (!snapshot.success) {
            valueText_ = L"--";
            usageRatio_ = 0.0f;
            return;
        }

        const QuotaBucket& bucket = (kind_ == Kind::Premium) ? snapshot.premium :
                                    (kind_ == Kind::Chat) ? snapshot.chat : snapshot.completions;

        valueText_ = FormatBucketValue(bucket, hidePercent);
        usageRatio_ = GetBucketUsageRatio(bucket);
    }

private:
    Kind kind_;
    const wchar_t* name_;
    const wchar_t* id_;
    const wchar_t* label_;
    const wchar_t* sample_;
    std::wstring valueText_ = L"--";
    float usageRatio_ = 0.0f;
};

class CopilotUsagePlugin final : public ITMPlugin {
public:
    static CopilotUsagePlugin& Instance() {
        static CopilotUsagePlugin plugin;
        return plugin;
    }

    int GetAPIVersion() const override { return 8; }

    IPluginItem* GetItem(int index) override {
        switch (index) {
        case 0: return &premiumItem_;
        case 1: return &chatItem_;
        case 2: return &completionsItem_;
        default: return nullptr;
        }
    }

    void DataRequired() override {
        const auto now = std::chrono::steady_clock::now();
        CopilotUsageConfig config;
        unsigned long long generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (fetchInProgress_) return;

            const int intervalSec = (config_.refreshInterval < 15) ? 15 : config_.refreshInterval;
            const auto minInterval = std::chrono::seconds(intervalSec);

            if (!forceRefresh_ && lastFetch_.time_since_epoch().count() != 0 && (now - lastFetch_) < minInterval) {
                return;
            }

            fetchInProgress_ = true;
            forceRefresh_ = false;
            lastFetch_ = now;
            config = config_;
            generation = configGeneration_;
        }

        std::thread([this, config, generation]() {
            CopilotUsageSnapshot snapshot = fetcher_.Fetch(config);

            std::lock_guard<std::mutex> lock(mutex_);
            fetchInProgress_ = false;
            if (generation != configGeneration_) return;

            snapshot_ = std::move(snapshot);
            premiumItem_.SetSnapshot(snapshot_, hidePercent_);
            chatItem_.SetSnapshot(snapshot_, hidePercent_);
            completionsItem_.SetSnapshot(snapshot_, hidePercent_);
            tooltip_ = BuildCopilotTooltip(snapshot_);
        }).detach();
    }

    OptionReturn ShowOptionsDialog(void* parent) override {
        CopilotUsageConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
        }

        if (!ShowCopilotOptions(reinterpret_cast<HWND>(parent), &config)) {
            return OR_OPTION_UNCHANGED;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = std::move(config);
            ++configGeneration_;
            lastFetch_ = {};
            forceRefresh_ = true;
            snapshot_ = {};
            premiumItem_.SetSnapshot(snapshot_, hidePercent_);
            chatItem_.SetSnapshot(snapshot_, hidePercent_);
            completionsItem_.SetSnapshot(snapshot_, hidePercent_);
            tooltip_ = L"GitHub Copilot Usage: waiting for data...";
        }

        DataRequired();
        return OR_OPTION_CHANGED;
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"Copilot Usage";
        case TMI_DESCRIPTION: return L"Displays GitHub Copilot quota and remaining usage in TrafficMonitor.";
        case TMI_AUTHOR:      return L"TrafficMonitor Community";
        case TMI_COPYRIGHT:   return L"MIT License";
        case TMI_VERSION:     return COPILOT_USAGE_VERSION_WIDE;
        case TMI_URL:         return L"https://github.com/zhongyang219/TrafficMonitor";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return tooltip_.c_str();
    }

    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override {
        if (index == EI_CONFIG_DIR && data && *data) {
            SetConfigDirectory(data);
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = LoadCopilotConfig();
            forceRefresh_ = true;
        } else if (index == EI_TASKBAR_WND_NOT_SHOW_PERCENT && data) {
            bool hide = (*data == L'1' || _wcsicmp(data, L"true") == 0);
            std::lock_guard<std::mutex> lock(mutex_);
            hidePercent_ = hide;
            premiumItem_.SetSnapshot(snapshot_, hidePercent_);
            chatItem_.SetSnapshot(snapshot_, hidePercent_);
            completionsItem_.SetSnapshot(snapshot_, hidePercent_);
        }
    }

    void OnInitialize(ITrafficMonitor* pApp) override {
        app_ = pApp;
    }

private:
    CopilotUsagePlugin() {
        config_ = LoadCopilotConfig();
    }

    CopilotUsageConfig config_;
    unsigned long long configGeneration_ = 0;
    CopilotUsageFetcher fetcher_;
    bool hidePercent_ = false;

    CopilotDisplayItem premiumItem_{
        CopilotDisplayItem::Kind::Premium,
        L"Copilot Premium Remaining",
        L"copilot_premium_remaining",
        L"CP Prem: ",
        L"100.0%"
    };

    CopilotDisplayItem chatItem_{
        CopilotDisplayItem::Kind::Chat,
        L"Copilot Chat Remaining",
        L"copilot_chat_remaining",
        L"CP Chat: ",
        L"100%"
    };

    CopilotDisplayItem completionsItem_{
        CopilotDisplayItem::Kind::Completions,
        L"Copilot Completions Remaining",
        L"copilot_completions_remaining",
        L"CP Comp: ",
        L"100%"
    };

    mutable std::mutex mutex_;
    CopilotUsageSnapshot snapshot_;
    std::wstring tooltip_ = L"GitHub Copilot Usage: waiting for data...";
    bool fetchInProgress_ = false;
    bool forceRefresh_ = true;
    std::chrono::steady_clock::time_point lastFetch_{};
    ITrafficMonitor* app_ = nullptr;
};

}  // namespace

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &CopilotUsagePlugin::Instance();
}

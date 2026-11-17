#include "CopilotUsageOptions.h"
#include "resource.h"

#include <commdlg.h>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::wstring ReadEdit(HWND dialog, int id) {
    HWND edit = GetDlgItem(dialog, id);
    if (!edit) return {};
    int len = GetWindowTextLengthW(edit);
    std::vector<wchar_t> text(static_cast<size_t>(len) + 1);
    GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
    return text.data();
}

INT_PTR CALLBACK OptionsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* config = reinterpret_cast<CopilotUsageConfig*>(GetWindowLongPtrW(dialog, DWLP_USER));

    if (message == WM_INITDIALOG) {
        config = reinterpret_cast<CopilotUsageConfig*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);

        SetDlgItemTextW(dialog, IDC_AUTH_PATH, config->authPath.c_str());
        SetDlgItemTextW(dialog, IDC_TOKEN, config->token.c_str());
        SetDlgItemTextW(dialog, IDC_PROXY_SERVER, config->proxyServer.c_str());
        SetDlgItemTextW(dialog, IDC_REFRESH_INTERVAL, std::to_wstring(config->refreshInterval).c_str());

        // Proxy mode ordering matching enum ProxyMode: 0=System, 1=Direct, 2=Manual
        SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System proxy (auto-detect / PAC)"));
        SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Direct connection (no proxy)"));
        SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Manual HTTP proxy"));

        int sel = static_cast<int>(config->proxyMode);
        if (sel < 0 || sel > 2) sel = 1;
        SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_SETCURSEL, sel, 0);

        EnableWindow(GetDlgItem(dialog, IDC_PROXY_SERVER), config->proxyMode == ProxyMode::Manual);
        return TRUE;
    }

    if (message == WM_COMMAND) {
        switch (LOWORD(wparam)) {
        case IDC_PROXY_MODE:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                int cur = static_cast<int>(SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_GETCURSEL, 0, 0));
                EnableWindow(GetDlgItem(dialog, IDC_PROXY_SERVER), cur == 2);
            }
            return TRUE;

        case IDC_BROWSE_AUTH: {
            std::vector<wchar_t> file(32768);
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = dialog;
            ofn.lpstrFilter = L"JSON / Config Files (*.json;*.yml;*.yaml;*.txt)\0*.json;*.yml;*.yaml;*.txt\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file.data();
            ofn.nMaxFile = static_cast<DWORD>(file.size());
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                SetDlgItemTextW(dialog, IDC_AUTH_PATH, file.data());
            }
            return TRUE;
        }

        case IDOK: {
            auto next = *config;
            next.authPath = ReadEdit(dialog, IDC_AUTH_PATH);
            next.token = ReadEdit(dialog, IDC_TOKEN);
            next.proxyServer = ReadEdit(dialog, IDC_PROXY_SERVER);

            int cur = static_cast<int>(SendDlgItemMessageW(dialog, IDC_PROXY_MODE, CB_GETCURSEL, 0, 0));
            next.proxyMode = (cur >= 0 && cur <= 2) ? static_cast<ProxyMode>(cur) : ProxyMode::Direct;

            std::wstring intervalStr = ReadEdit(dialog, IDC_REFRESH_INTERVAL);
            try {
                next.refreshInterval = std::stoi(intervalStr);
            } catch (...) {
                next.refreshInterval = 60;
            }

            std::wstring error;
            if (!NormalizeCopilotConfig(&next, &error) || !SaveCopilotConfig(next, &error)) {
                MessageBoxW(dialog, error.c_str(), L"Copilot Usage", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            *config = std::move(next);
            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }

    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

}  // namespace

bool ShowCopilotOptions(HWND parent, CopilotUsageConfig* config) {
    const auto result = DialogBoxParamW(
        reinterpret_cast<HMODULE>(&__ImageBase),
        MAKEINTRESOURCEW(IDD_OPTIONS),
        parent,
        OptionsProc,
        reinterpret_cast<LPARAM>(config));

    if (result == -1) {
        MessageBoxW(parent, L"Failed to display Copilot Usage options dialog.", L"Copilot Usage", MB_OK | MB_ICONERROR);
    }
    return result == IDOK;
}

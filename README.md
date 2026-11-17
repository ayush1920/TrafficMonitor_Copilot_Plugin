# TrafficMonitor GitHub Copilot Usage Plugin (`CopilotUsage`)

[![Build and Release](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin/actions)
[![GitHub Release](https://img.shields.io/github/v/release/ayush1920/TrafficMonitor_Copilot_Plugin)](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20(x86%20%7C%20x64%20%7C%20ARM64)-lightgrey.svg)](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin)

A lightweight, native C++20 plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) that displays real-time **GitHub Copilot** quota, rate limit, and remaining interaction statistics directly on your Windows taskbar and main monitor window.

---

## 📸 Preview

### Taskbar Display
```text
[ ↑ 12.4 KB/s  ↓ 45.1 KB/s ] [ CP Prem: 48.7% ▰▰▰▰▱▱▱▱▱▱ ] [ CP Chat: Unlim ]
```

### Hover Tooltip
Hovering over the display item surfaces full account and tier quota details:
```text
GitHub Copilot Usage (business)
Account: ayush1920
Premium Requests: 48.7% (14,620 / 30,000)
Chat: Unlimited
Completions: Unlimited
Reset Date: 2026-12-01
```

*(For Free/Individual tier accounts, Chat and Completion counters are displayed with their exact numerical limits, e.g., `Chat: 100.0% (200 / 200)` and `Completions: 99.8% (1,997 / 2,000)`).*

---

## 🌟 Key Features

- **Three Independent Display Items**:
  - `Copilot Premium Remaining` (`copilot_premium_remaining`): Percentage of premium interactions remaining, paired with TrafficMonitor's native resource usage graph bar.
  - `Copilot Chat Remaining` (`copilot_chat_remaining`): Percentage or `Unlim` status of chat requests.
  - `Copilot Completions Remaining` (`copilot_completions_remaining`): Percentage or `Unlim` status of code completion requests.
- **Zero-Configuration Credential Discovery**:
  - **Windows Credential Manager Integration**: Seamlessly retrieves stored tokens from GitHub CLI (`gh:github.com:*`), Copilot CLI, or Visual Studio.
  - **GitHub CLI Account Switching**: Switching accounts in `gh` (`gh auth switch --user <username>`) is instantly detected without manual re-configuration.
  - **IDE Auth Files**: Checks standard paths such as `%LOCALAPPDATA%\github-copilot\hosts.json` and `%USERPROFILE%\.config\github-copilot\hosts.json`.
  - **Environment Variables**: Reads `COPILOT_TOKEN`, `GH_TOKEN`, or `GITHUB_TOKEN`.
  - **Manual Override**: Allows direct entry of a token or pointing to a custom JSON / YAML / text file.
- **Corporate & Custom Proxy Support**:
  - **Direct Connection** (default, recommended).
  - **System Proxy** (automatic Windows proxy, PAC, WPAD).
  - **Manual HTTP Proxy** (e.g., `127.0.0.1:7890` or `http://proxy.corp.net:8080`).
- **Smooth, Non-Blocking Execution**:
  - HTTP requests run asynchronously via WinHTTP on a detached worker thread; the taskbar UI never stutters or lags.
  - Configurable polling interval (default: 60s, minimum safety threshold: 15s to respect GitHub API limits).

---

## 📥 Installation

### Method 1: Using Pre-compiled DLLs (Recommended)

1. Download the latest release from the [Releases](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin/releases) page:
   - For 64-bit Windows (most common): `CopilotUsage-x64.zip`
   - For 32-bit Windows: `CopilotUsage-x86.zip`
   - For ARM64 Windows: `CopilotUsage-arm64.zip`
2. Locate your **TrafficMonitor** installation directory (where `TrafficMonitor.exe` resides).
3. If a `plugins` subfolder does not exist, create it:
   ```text
   <TrafficMonitor_Directory>\plugins\
   ```
4. Extract `CopilotUsage.dll` into the `plugins` folder:
   ```text
   <TrafficMonitor_Directory>\plugins\CopilotUsage.dll
   ```
5. Restart **TrafficMonitor**.
6. Enable the items on your taskbar:
   - Right-click the TrafficMonitor taskbar or system tray icon -> select **More Options...** (或 **选项设置**).
   - In **Plugin Manage** (插件管理), ensure **Copilot Usage** is present and marked *Enabled*.
   - In **Taskbar Window Settings** (任务栏窗口设置), check the items you want to display:
     - ☑ `Copilot Premium Remaining`
     - ☑ `Copilot Chat Remaining`
     - ☑ `Copilot Completions Remaining`
   - Click **OK**.

---

## ⚙️ Configuration & Options Dialog

To open the settings dialog:
Right-click TrafficMonitor -> **More Options...** -> **Plugin Manage** -> Select **Copilot Usage** -> Click **Options...** (选项).

| Option | Description | Default |
| :--- | :--- | :--- |
| **Access Token File** | Path to a custom `hosts.json`, `hosts.yml`, or token file. | *(empty = auto-detect)* |
| **Manual Token Override** | Direct personal access token or OAuth token (`gho_...`). | *(empty = auto-detect)* |
| **Proxy Mode** | Choose between `Direct connection`, `System proxy`, or `Manual HTTP proxy`. | `Direct connection` |
| **HTTP Proxy** | Host and port for manual proxy (e.g., `127.0.0.1:7890`). | *(empty)* |
| **Refresh (sec)** | Time in seconds between background queries. Minimum is 15s. | `60` |

Settings are automatically persisted to `CopilotUsage.ini` in the plugin directory.

### Manual INI Editing (`CopilotUsage.ini`)
You can also create or edit `CopilotUsage.ini` manually alongside `CopilotUsage.dll`:
```ini
[CopilotUsage]
token=
auth_path=
proxy_mode=direct
proxy_server=
refresh_interval=60
```
- `proxy_mode`: `direct`, `system`, or `manual`.
- `refresh_interval`: Integer in seconds (e.g., `30` or `60`).

---

## 🔑 Authentication Guide

### Automatic (Recommended)
If you have logged in via any of the following, no configuration is needed:
1. **GitHub CLI**: Run `gh auth login` once in your terminal.
2. **VS Code / Visual Studio / JetBrains Copilot Extension**: The plugin automatically detects the cached tokens.

### Switching GitHub Accounts
If you use multiple GitHub accounts, you can switch the active account at any time via GitHub CLI:
```powershell
gh auth switch --user <your_username>
```
The plugin detects the switch on its next refresh cycle and updates your taskbar display immediately.

### Manual Token Entry
If you prefer an explicit token:
1. Generate or copy your GitHub OAuth token or Personal Access Token with Copilot access.
2. Open **Plugin Manage** -> **Copilot Usage** -> **Options...**.
3. Paste the token into the **Manual Token Override** field and click **OK**.

---

## 🛠️ Building from Source

### Prerequisites
- Windows 10 or 11 (x86, x64, or ARM64)
- CMake 3.21 or newer
- Visual Studio 2022 (MSVC with C++20 support) or Visual Studio Build Tools
- Windows SDK (10.0.19041.0 or newer)

### Build Steps (PowerShell)
```powershell
# Clone the repository
git clone https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin.git
cd TrafficMonitor_Copilot_Plugin

# Configure build with CMake (specify architecture, e.g. x64, Win32, or ARM64)
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release

# Build the DLL and test executable
cmake --build build --config Release

# Run automated tests
ctest --test-dir build -C Release --output-on-failure
```

The compiled binaries will be generated at:
- `build/Release/CopilotUsage.dll`
- `build/Release/CopilotUsageTests.exe`

---

## 📁 Project Architecture

```text
TrafficMonitor_Copilot_Plugin/
├── .github/
│   └── workflows/
│       └── build-and-release.yml   # Multi-platform CI (x86, x64, arm64) & automated releases
├── include/
│   └── PluginInterface.h          # TrafficMonitor Plugin SDK (API version 8)
├── src/
│   ├── CopilotUsageCore.h/.cpp    # Core quota bucket data models & JSON parser
│   ├── CopilotUsageConfig.h/.cpp  # INI configuration loader/saver with atomic write
│   ├── CopilotUsageFetcher.h/.cpp # WinHTTP client & multi-tier credential auto-discovery
│   ├── CopilotUsageOptions.h/.cpp # Native Win32 modal options dialog handler
│   ├── CopilotUsagePlugin.cpp     # TrafficMonitor ITMPlugin & IPluginItem implementation
│   ├── CopilotUsageVersion.h.in   # CMake version template
│   ├── JsonLite.h/.cpp            # Zero-dependency lightweight JSON parser
│   ├── resource.h                 # Win32 resource control identifiers
│   └── CopilotUsage.rc            # Dialog layout & string tables
├── tests/
│   └── CopilotUsageTests.cpp      # Unit test suite covering parser, config & live API
├── CMakeLists.txt
├── LICENSE                        # MIT License
└── README.md
```

---

## ❓ Troubleshooting & FAQ

<details>
<summary><b>1. The plugin does not appear in TrafficMonitor's Plugin Manager</b></summary>

- Verify that `CopilotUsage.dll` is placed in the `plugins/` directory right next to `TrafficMonitor.exe`.
- Make sure the DLL architecture matches your `TrafficMonitor.exe` architecture (usually **x64**).
- Completely exit TrafficMonitor (right-click icon -> **Exit** / 退出) and restart it.
</details>

<details>
<summary><b>2. The taskbar displays "ERR" or tooltip shows "HTTP 401"</b></summary>

- Your token may be expired or lack Copilot permissions.
- Run `gh auth status` in your terminal to ensure you are logged into a GitHub account with an active Copilot subscription.
- Run `gh auth login` or paste your token into **Plugin Options** -> **Manual Token Override**.
</details>

<details>
<summary><b>3. I am behind a corporate proxy or VPN</b></summary>

- Open **Plugin Options** (选项设置 -> 插件管理 -> Copilot Usage -> 选项).
- Select **System proxy** if your computer uses Windows system proxy or PAC scripts.
- Alternatively, select **Manual HTTP proxy** and specify your proxy address (e.g. `127.0.0.1:7890` or `proxy.company.com:8080`).
</details>

<details>
<summary><b>4. How does this plugin compare to TrafficMonitor_Codex_Plugin?</b></summary>

- While `TrafficMonitor_Codex_Plugin` queries OpenAI Codex / ChatGPT rate limits, this plugin queries official **GitHub Copilot** quotas (`/copilot_internal/user`), including Premium Requests, Chat, and Completions. Both follow the same clean, non-blocking C++ architecture and can run side-by-side.
</details>

---

## 🤝 Acknowledgements

- [zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) for the excellent system monitoring software and plugin framework.
- [HCLonely/TrafficMonitor_Codex_Plugin](https://github.com/HCLonely/TrafficMonitor_Codex_Plugin) for the architectural blueprint of TrafficMonitor usage plugins.

---

## 📄 License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for details.

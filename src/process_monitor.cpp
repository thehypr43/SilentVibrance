#include "process_monitor.h"
#include <psapi.h>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

ForegroundMonitor* ForegroundMonitor::active_ = nullptr;

static std::wstring ProcessPath(HWND window) {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (!pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0'); DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) size = 0;
    CloseHandle(process); path.resize(size); return path;
}

ForegroundMonitor::~ForegroundMonitor() { Stop(); }
bool ForegroundMonitor::Start(Callback callback) {
    Stop(); callback_ = std::move(callback); active_ = this;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    return hook_ != nullptr;
}
void ForegroundMonitor::Stop() { if (hook_) UnhookWinEvent(hook_); hook_ = nullptr; if (active_ == this) active_ = nullptr; }
void CALLBACK ForegroundMonitor::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (active_ && active_->callback_) active_->callback_();
}
std::wstring ForegroundMonitor::ForegroundExecutable() {
    const auto path = ProcessPath(GetForegroundWindow());
    return path.empty() ? L"" : std::filesystem::path(path).filename().wstring();
}

std::vector<RunningApplication> EnumerateRunningApplications() {
    struct Data { std::vector<RunningApplication> items; std::unordered_set<std::wstring> paths; } data;
    EnumWindows([](HWND w, LPARAM p) -> BOOL {
        if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
        auto& d = *reinterpret_cast<Data*>(p); auto path = ProcessPath(w); if (path.empty()) return TRUE;
        std::wstring folded = path; std::transform(folded.begin(), folded.end(), folded.begin(), towlower);
        if (!d.paths.insert(folded).second) return TRUE;
        wchar_t title[512]{}; GetWindowTextW(w, title, 512);
        const auto file = std::filesystem::path(path).filename().wstring();
        d.items.push_back({title[0] ? title : file, file, path}); return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    std::sort(data.items.begin(), data.items.end(), [](const auto& a, const auto& b){ return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return data.items;
}


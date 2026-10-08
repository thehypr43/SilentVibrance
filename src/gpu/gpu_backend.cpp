#include "gpu_backend.h"
#include <windows.h>
#include <vector>

std::unique_ptr<GpuBackend> CreateNvidiaBackend();
std::unique_ptr<GpuBackend> CreateAmdBackend();
std::unique_ptr<GpuBackend> CreateIntelBackend();

namespace {
#ifndef NDEBUG
void GpuTrace(const wchar_t* text) {
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp); std::wstring path = std::wstring(temp) + L"SilentVibrance-gpu-trace.txt";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { DWORD bytes; WriteFile(file, text, static_cast<DWORD>(wcslen(text) * sizeof(wchar_t)), &bytes, nullptr); WriteFile(file, L"\r\n", 4, &bytes, nullptr); CloseHandle(file); }
}
#else
void GpuTrace(const wchar_t*) {}
#endif
std::wstring PrimaryDisplayName() {
    POINT origin{0, 0}; HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, &info) ? info.szDevice : L"\\\\.\\DISPLAY1";
}
std::wstring PrimaryAdapterDescription(const std::wstring& display) {
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW adapter{}; adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0)) break;
        if (_wcsicmp(adapter.DeviceName, display.c_str()) == 0)
            return std::wstring(adapter.DeviceString) + L" " + adapter.DeviceID;
    }
    return L"";
}
}

std::unique_ptr<GpuBackend> CreateBestBackend(std::wstring& primaryDisplay, std::wstring& diagnostic) {
    GpuTrace(L"factory display");
    primaryDisplay = PrimaryDisplayName();
    GpuTrace(L"factory adapter");
    const std::wstring adapter = PrimaryAdapterDescription(primaryDisplay);
    GpuTrace(adapter.c_str());
    std::vector<std::unique_ptr<GpuBackend>> candidates;
    const bool amd = adapter.find(L"AMD") != std::wstring::npos || adapter.find(L"Radeon") != std::wstring::npos || adapter.find(L"VEN_1002") != std::wstring::npos;
    const bool nvidia = adapter.find(L"NVIDIA") != std::wstring::npos || adapter.find(L"VEN_10DE") != std::wstring::npos;
    if (amd) candidates.push_back(CreateAmdBackend());
    else if (nvidia) candidates.push_back(CreateNvidiaBackend());
    GpuTrace(L"factory candidates");
    for (auto& candidate : candidates) {
        GpuTrace(L"factory initialize candidate");
        if (candidate->Initialize(primaryDisplay)) {
            diagnostic = candidate->Name() + L" controls " + primaryDisplay + L" (" + adapter + L")";
            return std::move(candidate);
        }
        if (!diagnostic.empty()) diagnostic += L" | ";
        diagnostic += candidate->Status();
    }
    GpuTrace(L"factory intel");
    auto intel = CreateIntelBackend(); intel->Initialize(primaryDisplay);
    GpuTrace(L"factory intel initialized");
    diagnostic += (diagnostic.empty() ? L"" : L" | ") + intel->Status();
    return intel;
}

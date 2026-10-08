#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct RunningApplication { std::wstring name, executable, path; };

class ForegroundMonitor {
public:
    using Callback = std::function<void()>;
    ~ForegroundMonitor();
    bool Start(Callback callback);
    void Stop();
    static std::wstring ForegroundExecutable();
private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    HWINEVENTHOOK hook_ = nullptr;
    Callback callback_;
    static ForegroundMonitor* active_;
};

std::vector<RunningApplication> EnumerateRunningApplications();


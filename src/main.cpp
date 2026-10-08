#include "ui.h"
#include <windows.h>
#include <shellscalingapi.h>

static AppWindow* gApp = nullptr;
static LONG WINAPI RestoreOnCrash(EXCEPTION_POINTERS*) { if(gApp) gApp->EmergencyRestore(); return EXCEPTION_CONTINUE_SEARCH; }

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SilentVibrance.Native.Instance.2");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = nullptr;
        for (int i=0;i<30 && !other;++i){ other=FindWindowW(AppWindow::ClassName,nullptr); if(!other)Sleep(50); }
        if(other) SendMessageTimeoutW(other,AppWindow::ShowExistingMessage,0,0,SMTO_ABORTIFHUNG,2000,nullptr);
        CloseHandle(mutex); return 0;
    }
    SetUnhandledExceptionFilter(RestoreOnCrash);
    AppWindow app(instance); gApp=&app;
    const bool startup = wcsstr(GetCommandLineW(),L"--startup") != nullptr;
    int result=app.Run(startup); gApp=nullptr; ReleaseMutex(mutex); CloseHandle(mutex); return result;
}

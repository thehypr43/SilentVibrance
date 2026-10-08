#include <windows.h>
#include <windowsx.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

class UserStateGuard {
public:
    explicit UserStateGuard(std::wstring settingsPath) : settingsPath_(std::move(settingsPath)) {
        std::ifstream input(settingsPath_.c_str(),std::ios::binary);if(input){fileExisted_=true;fileData_={std::istreambuf_iterator<char>(input),{}};}
        HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS){DWORD bytes=0;if(RegQueryValueExW(key,L"SilentVibrance",nullptr,&runType_,nullptr,&bytes)==ERROR_SUCCESS){runExisted_=true;runData_.resize(bytes);RegQueryValueExW(key,L"SilentVibrance",nullptr,&runType_,runData_.data(),&bytes);}RegCloseKey(key);}
    }
    ~UserStateGuard(){
        if(fileExisted_){std::ofstream output(settingsPath_.c_str(),std::ios::binary|std::ios::trunc);output.write(fileData_.data(),static_cast<std::streamsize>(fileData_.size()));}else DeleteFileW(settingsPath_.c_str());
        HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){if(runExisted_)RegSetValueExW(key,L"SilentVibrance",0,runType_,runData_.data(),static_cast<DWORD>(runData_.size()));else RegDeleteValueW(key,L"SilentVibrance");RegCloseKey(key);}
    }
private:
    std::wstring settingsPath_;bool fileExisted_{};std::vector<char> fileData_;bool runExisted_{};DWORD runType_{REG_SZ};std::vector<BYTE> runData_;
};

static DWORD targetPid;
static HWND targetWindow;
static bool profileStatus;
static bool defaultStatus;
static BOOL CALLBACK FindMain(HWND window, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid); if (pid != targetPid) return TRUE;
    wchar_t cls[128]{}; GetClassNameW(window, cls, 128);
    if (wcscmp(cls, L"SilentVibrance.Native.Window.2") == 0) { targetWindow = window; return FALSE; }
    return TRUE;
}
static bool WaitWindow(DWORD pid) {
    targetPid = pid;
    for (int i=0;i<80;++i) { targetWindow=nullptr; EnumWindows(FindMain,0); if(targetWindow) return true; Sleep(50); }
    return false;
}
static BOOL CALLBACK ReadStatus(HWND window, LPARAM mode) {
    wchar_t text[512]{}; GetWindowTextW(window,text,512);
    if (text[0] && (wcsstr(text,L"NVIDIA") || wcsstr(text,L"AMD") || wcsstr(text,L"Intel"))) std::wcout << L"status=" << text << L'\n';
    if(mode==1 && wcsstr(text,L"SilentVibrance_lifecycle.exe") && wcsstr(text,L"83")) profileStatus=true;
    if(mode==2 && wcsstr(text,L"default") && wcsstr(text,L"42")) defaultStatus=true;
    return TRUE;
}
static void Check(bool value, const char* text, int& failures) { std::cout << text << '=' << (value?"PASS":"FAIL") << '\n'; if(!value) ++failures; }
static unsigned long long FileTime64(FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime; }
static int ReadNvidiaDvc() {
    using Q=void*(__cdecl*)(unsigned); using F=int(__cdecl*)(); using H=int(__cdecl*)(const char*,void**); using G=int(__cdecl*)(void*,unsigned,void*);
    struct D { unsigned version; int current,minimum,maximum,defaultValue; } d{sizeof(D)|(1u<<16)};
    HMODULE module=LoadLibraryW(L"nvapi64.dll"); if(!module)return -999; auto q=reinterpret_cast<Q>(GetProcAddress(module,"nvapi_QueryInterface")); if(!q){FreeLibrary(module);return -999;}
    auto init=reinterpret_cast<F>(q(0x0150e828));auto unload=reinterpret_cast<F>(q(0xd22bdd7e));auto handle=reinterpret_cast<H>(q(0x35c29134));auto get=reinterpret_cast<G>(q(0x0e45002d));
    MONITORINFOEXA monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoA(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);void* display=nullptr;
    int result=(init&&unload&&handle&&get&&init()==0&&handle(monitor.szDevice,&display)==0&&display&&get(display,0,&d)==0)?d.current:-999;if(unload)unload();FreeLibrary(module);return result;
}
static bool ForceForeground(HWND window) {
    for(int attempt=0;attempt<10;++attempt){HWND current=GetForegroundWindow();DWORD foreign=GetWindowThreadProcessId(current,nullptr),own=GetCurrentThreadId();if(foreign!=own)AttachThreadInput(own,foreign,TRUE);
        keybd_event(VK_MENU,0,0,0);keybd_event(VK_MENU,0,KEYEVENTF_KEYUP,0);ShowWindow(window,SW_RESTORE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);BringWindowToTop(window);SetForegroundWindow(window);SetFocus(window);
        if(foreign!=own)AttachThreadInput(own,foreign,FALSE);if(GetForegroundWindow()==window)return true;Sleep(50);}return false;
}

int wmain(int argc, wchar_t** argv) {
    if(argc!=2) return 2; int failures=0;
    wchar_t local[32768]{}; GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768); std::wstring dir=std::wstring(local)+L"\\SilentVibrance"; CreateDirectoryW(dir.c_str(),nullptr); std::wstring ini=dir+L"\\settings.ini";
    UserStateGuard restoreUserState(ini);
    WritePrivateProfileStringW(L"General",L"DefaultVibrance",L"42",ini.c_str()); WritePrivateProfileStringW(L"General",L"StartWithWindows",L"0",ini.c_str()); WritePrivateProfileStringW(L"General",L"ApplicationCount",L"1",ini.c_str());
    WritePrivateProfileStringW(L"Application0",L"Name",L"Lifecycle probe",ini.c_str()); WritePrivateProfileStringW(L"Application0",L"Executable",L"SilentVibrance_lifecycle.exe",ini.c_str()); WritePrivateProfileStringW(L"Application0",L"Path",argv[0],ini.c_str()); WritePrivateProfileStringW(L"Application0",L"Vibrance",L"83",ini.c_str()); WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());
    std::wstring command=L"\""+std::wstring(argv[1])+L"\" --startup";
    const int dvcBefore=ReadNvidiaDvc(); STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION first{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&first)) return 3;
    Check(WaitWindow(first.dwProcessId),"window-created",failures); if(!targetWindow){TerminateProcess(first.hProcess,4);return 4;}
    Check(!IsWindowVisible(targetWindow),"startup-hidden",failures);
    std::wstring secondCommand=L"\""+std::wstring(argv[1])+L"\""; PROCESS_INFORMATION second{};
    Check(CreateProcessW(nullptr,secondCommand.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&second),"second-launched",failures);
    Check(WaitForSingleObject(second.hProcess,3000)==WAIT_OBJECT_0,"second-exited",failures); Sleep(200);
    Check(IsWindowVisible(targetWindow),"second-shows-existing",failures);
    ShowWindow(targetWindow,SW_MINIMIZE); Sleep(100); Check(IsIconic(targetWindow),"minimize",failures);
    ShowWindow(targetWindow,SW_RESTORE); SendMessageW(targetWindow,WM_CLOSE,0,0); Sleep(100);
    Check(!IsWindowVisible(targetWindow)&&WaitForSingleObject(first.hProcess,0)==WAIT_TIMEOUT,"close-hides",failures);
    ShowWindow(targetWindow,SW_RESTORE);
    WNDCLASSW probeClass{}; probeClass.lpfnWndProc=DefWindowProcW; probeClass.hInstance=GetModuleHandleW(nullptr); probeClass.lpszClassName=L"SilentVibrance.Lifecycle.Probe"; RegisterClassW(&probeClass);
    HWND probe=CreateWindowW(probeClass.lpszClassName,L"Foreground probe",WS_OVERLAPPEDWINDOW,50,50,300,150,nullptr,nullptr,probeClass.hInstance,nullptr); ShowWindow(probe,SW_SHOW); Check(ForceForeground(probe),"probe-foreground",failures); PostMessageW(targetWindow,WM_APP+10,0,0); Sleep(300);
    EnumChildWindows(targetWindow,ReadStatus,1); Check(profileStatus,"per-app-vibrance",failures);
    Check(ForceForeground(targetWindow),"main-foreground",failures); PostMessageW(targetWindow,WM_APP+10,0,0); Sleep(300); EnumChildWindows(targetWindow,ReadStatus,2); Check(defaultStatus,"default-restored",failures); DestroyWindow(probe);
    HWND startup=GetDlgItem(targetWindow,106); Check(startup!=nullptr,"startup-checkbox",failures);
    if(Button_GetCheck(startup)==BST_CHECKED) SendMessageW(startup,BM_CLICK,0,0);
    SendMessageW(startup,BM_CLICK,0,0); Sleep(100); HKEY key{}; wchar_t value[32768]{}; DWORD bytes=sizeof(value),type=0;
    LONG opened=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key);
    LONG read=opened==ERROR_SUCCESS?RegQueryValueExW(key,L"SilentVibrance",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes):opened;
    if(key)RegCloseKey(key); Check(read==ERROR_SUCCESS&&wcsstr(value,L"--startup"),"autostart-enable",failures);
    SendMessageW(startup,BM_CLICK,0,0); Sleep(100); key=nullptr; opened=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key);
    read=opened==ERROR_SUCCESS?RegQueryValueExW(key,L"SilentVibrance",nullptr,&type,nullptr,&bytes):opened; if(key)RegCloseKey(key);
    Check(read==ERROR_FILE_NOT_FOUND,"autostart-disable",failures);
    PROCESS_MEMORY_COUNTERS pmc{}; pmc.cb=sizeof(pmc); GetProcessMemoryInfo(first.hProcess,&pmc,sizeof(pmc));
    FILETIME a,b,k0,u0,k1,u1; GetProcessTimes(first.hProcess,&a,&b,&k0,&u0); Sleep(3000); GetProcessTimes(first.hProcess,&a,&b,&k1,&u1);
    auto cpu100ns=(FileTime64(k1)-FileTime64(k0))+(FileTime64(u1)-FileTime64(u0));
    std::cout<<"working-set-bytes="<<pmc.WorkingSetSize<<"\nidle-cpu-ms-3s="<<(cpu100ns/10000)<<'\n';
    SendMessageW(targetWindow,WM_COMMAND,107,0); Check(WaitForSingleObject(first.hProcess,3000)==WAIT_OBJECT_0,"exit-button",failures);
    DWORD exitCode=999; GetExitCodeProcess(first.hProcess,&exitCode); Check(exitCode==0,"exit-code",failures);
    const int dvcAfter=ReadNvidiaDvc(); if(dvcBefore!=-999){std::cout<<"dvc-before="<<dvcBefore<<"\ndvc-after="<<dvcAfter<<'\n';Check(dvcAfter==dvcBefore,"driver-value-restored",failures);}else std::cout<<"driver-value-restored=SKIP(non-NVIDIA primary)\n";
    CloseHandle(first.hThread);CloseHandle(first.hProcess);CloseHandle(second.hThread);CloseHandle(second.hProcess);
    return failures?1:0;
}

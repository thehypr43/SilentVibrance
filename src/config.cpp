#include "config.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace {
std::wstring DefaultPath() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) return L"settings.ini";
    std::wstring dir(raw); CoTaskMemFree(raw);
    const std::wstring root(dir);
    dir += L"\\SilentVibrance";
    std::filesystem::create_directories(dir);
    const std::wstring file = dir + L"\\settings.ini";
    // Carry over settings written under the old VibranceGUI name.
    std::error_code ignored;
    if (!std::filesystem::exists(file, ignored))
        std::filesystem::copy_file(root + L"\\VibranceGUI\\settings.ini", file, ignored);
    return file;
}
std::wstring ReadString(const std::wstring& file, const std::wstring& section, const wchar_t* key) {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section.c_str(), key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
    return buffer.data();
}
}

ConfigStore::ConfigStore(std::wstring overridePath) : path_(overridePath.empty() ? DefaultPath() : std::move(overridePath)) {}

bool ConfigStore::Load(Settings& settings, std::wstring& error) const {
    error.clear();
    settings = {};
    settings.defaultVibrance = ClampVibrance(GetPrivateProfileIntW(L"General", L"DefaultVibrance", 50, path_.c_str()));
    settings.startWithWindows = GetPrivateProfileIntW(L"General", L"StartWithWindows", 0, path_.c_str()) != 0;
    const int count = std::clamp<int>(GetPrivateProfileIntW(L"General", L"ApplicationCount", 0, path_.c_str()), 0, 2048);
    for (int i = 0; i < count; ++i) {
        const std::wstring section = L"Application" + std::to_wstring(i);
        AppProfile p;
        p.name = ReadString(path_, section, L"Name");
        p.executable = ReadString(path_, section, L"Executable");
        p.path = ReadString(path_, section, L"Path");
        p.vibrance = ClampVibrance(GetPrivateProfileIntW(section.c_str(), L"Vibrance", 70, path_.c_str()));
        if (!p.executable.empty()) settings.applications.push_back(std::move(p));
    }
    return true;
}

bool ConfigStore::Save(const Settings& settings, std::wstring& error) const {
    error.clear();
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
    DeleteFileW(path_.c_str());
    HANDLE fresh = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fresh == INVALID_HANDLE_VALUE) { error = L"Could not create configuration: " + path_; return false; }
    const wchar_t bom = 0xFEFF; DWORD written = 0; WriteFile(fresh, &bom, sizeof(bom), &written, nullptr); CloseHandle(fresh);
    auto write = [&](const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(section, key, value.c_str(), path_.c_str()) != FALSE;
    };
    bool ok = write(L"General", L"DefaultVibrance", std::to_wstring(ClampVibrance(settings.defaultVibrance))) &&
              write(L"General", L"StartWithWindows", settings.startWithWindows ? L"1" : L"0") &&
              write(L"General", L"ApplicationCount", std::to_wstring(settings.applications.size()));
    for (size_t i = 0; ok && i < settings.applications.size(); ++i) {
        const auto& p = settings.applications[i];
        const std::wstring section = L"Application" + std::to_wstring(i);
        ok = write(section.c_str(), L"Name", p.name) && write(section.c_str(), L"Executable", p.executable) &&
             write(section.c_str(), L"Path", p.path) && write(section.c_str(), L"Vibrance", std::to_wstring(ClampVibrance(p.vibrance)));
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());
    if (!ok) error = L"Could not write configuration: " + path_;
    return ok;
}

bool SetStartWithWindows(bool enabled, const std::wstring& executablePath, std::wstring& error) {
    error.clear(); HKEY key = nullptr;
    constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRun, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        error = L"Could not open the per-user startup registry key."; return false;
    }
    RegDeleteValueW(key, L"VibranceGUI"); // drop the entry left by older builds
    const std::wstring command = L"\"" + executablePath + L"\" --startup";
    LONG result = enabled ? RegSetValueExW(key, L"SilentVibrance", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
        : RegDeleteValueW(key, L"SilentVibrance");
    RegCloseKey(key);
    if (!enabled && result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    if (result != ERROR_SUCCESS) error = L"Could not update Start with Windows.";
    return result == ERROR_SUCCESS;
}

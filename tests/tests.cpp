#include "model.h"
#include "config.h"
#include <windows.h>
#include <filesystem>
#include <iostream>

static int failures = 0;
static void Check(bool value, const char* label) { if (!value) { std::cerr << "FAIL: " << label << '\n'; ++failures; } }

int wmain() {
    Check(ClampVibrance(-1) == 0, "lower clamp");
    Check(ClampVibrance(101) == 100, "upper clamp");
    Settings source; source.defaultVibrance = 54; source.startWithWindows = true;
    source.applications.push_back({L"Counter-Strike 2 Türkçe", L"cs2.exe", L"C:\\Oyunlar\\Çağrı\\cs2.exe", 91});
    Check(FindProfileForExecutable(source, L"CS2.EXE") == 0, "case-insensitive executable match");
    Check(FindProfileForExecutable(source, L"other.exe") == -1, "unknown executable");

    auto path = std::filesystem::temp_directory_path() / L"SilentVibrance-tests" / L"settings.ini";
    std::filesystem::remove_all(path.parent_path());
    ConfigStore store(path.wstring()); std::wstring error;
    Check(store.Save(source, error), "save config");
    Settings loaded; Check(store.Load(loaded, error), "load config");
    Check(loaded.defaultVibrance == 54 && loaded.startWithWindows, "general round trip");
    Check(loaded.applications.size() == 1 && loaded.applications[0].path == source.applications[0].path && loaded.applications[0].vibrance == 91, "profile round trip");
    std::filesystem::remove_all(path.parent_path());
    if (failures) return 1;
    std::cout << "PASS: 7 checks\n";
    return 0;
}

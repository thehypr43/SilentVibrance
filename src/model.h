#pragma once

#include <string>
#include <vector>

struct AppProfile {
    std::wstring name;
    std::wstring executable;
    std::wstring path;
    int vibrance = 70;
};

struct Settings {
    int defaultVibrance = 50;
    bool startWithWindows = false;
    std::vector<AppProfile> applications;
};

int ClampVibrance(int value);
int FindProfileForExecutable(const Settings& settings, const std::wstring& executable);


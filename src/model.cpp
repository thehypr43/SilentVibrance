#include "model.h"
#include <algorithm>
#include <cwctype>

int ClampVibrance(int value) { return std::clamp(value, 0, 100); }

static bool EqualsInsensitive(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

int FindProfileForExecutable(const Settings& settings, const std::wstring& executable) {
    for (size_t i = 0; i < settings.applications.size(); ++i)
        if (EqualsInsensitive(settings.applications[i].executable, executable)) return static_cast<int>(i);
    return -1;
}


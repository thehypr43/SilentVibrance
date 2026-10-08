#pragma once
#include "model.h"
#include <string>

class ConfigStore {
public:
    explicit ConfigStore(std::wstring overridePath = {});
    bool Load(Settings& settings, std::wstring& error) const;
    bool Save(const Settings& settings, std::wstring& error) const;
    const std::wstring& Path() const { return path_; }
private:
    std::wstring path_;
};

bool SetStartWithWindows(bool enabled, const std::wstring& executablePath, std::wstring& error);


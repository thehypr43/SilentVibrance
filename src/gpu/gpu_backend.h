#pragma once
#include <memory>
#include <string>

class GpuBackend {
public:
    virtual ~GpuBackend() = default;
    virtual bool Initialize(const std::wstring& primaryDisplay) = 0;
    virtual bool Apply(int normalizedValue) = 0;
    virtual void RestoreOriginal() noexcept = 0;
    virtual std::wstring Name() const = 0;
    virtual std::wstring Status() const = 0;
};

std::unique_ptr<GpuBackend> CreateBestBackend(std::wstring& primaryDisplay, std::wstring& diagnostic);


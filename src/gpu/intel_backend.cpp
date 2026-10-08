#include "gpu_backend.h"

namespace {
class IntelBackend final : public GpuBackend {
public:
    bool Initialize(const std::wstring&) override { return false; }
    bool Apply(int) override { return false; }
    void RestoreOriginal() noexcept override {}
    std::wstring Name() const override { return L"Unsupported"; }
    std::wstring Status() const override { return L"Intel/other: no public reliable saturation API; no display changes made"; }
};
}
std::unique_ptr<GpuBackend> CreateIntelBackend() { return std::make_unique<IntelBackend>(); }


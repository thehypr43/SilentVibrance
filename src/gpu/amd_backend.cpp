#include "gpu_backend.h"
#include "adl_sdk.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>

namespace {
void* __stdcall AdlAlloc(int bytes) { return std::malloc(static_cast<size_t>(bytes)); }
using MainCreate = int (*)(ADL_MAIN_MALLOC_CALLBACK, int);
using MainDestroy = int (*)();
using NumberAdapters = int (*)(int*);
using AdapterInfoGet = int (*)(LPAdapterInfo, int);
using DisplayInfoGet = int (*)(int, int*, ADLDisplayInfo**, int);
using ColorCapsGet = int (*)(int, int, int*, int*);
using ColorGet = int (*)(int, int, int, int*, int*, int*, int*, int*);
using ColorSet = int (*)(int, int, int, int);

class AmdBackend final : public GpuBackend {
public:
    ~AmdBackend() override { RestoreOriginal(); if (destroy_) destroy_(); if (module_) FreeLibrary(module_); }
    bool Initialize(const std::wstring& primaryDisplay) override {
        module_ = LoadLibraryW(L"atiadlxx.dll");
        if (!module_) { status_ = L"AMD: atiadlxx.dll not present"; return false; }
        auto proc = [&](const char* n){ return GetProcAddress(module_, n); };
        auto create = reinterpret_cast<MainCreate>(proc("ADL_Main_Control_Create")); destroy_ = reinterpret_cast<MainDestroy>(proc("ADL_Main_Control_Destroy"));
        auto countFn = reinterpret_cast<NumberAdapters>(proc("ADL_Adapter_NumberOfAdapters_Get"));
        auto infoFn = reinterpret_cast<AdapterInfoGet>(proc("ADL_Adapter_AdapterInfo_Get"));
        auto displaysFn = reinterpret_cast<DisplayInfoGet>(proc("ADL_Display_DisplayInfo_Get"));
        auto capsFn = reinterpret_cast<ColorCapsGet>(proc("ADL_Display_ColorCaps_Get"));
        get_ = reinterpret_cast<ColorGet>(proc("ADL_Display_Color_Get")); set_ = reinterpret_cast<ColorSet>(proc("ADL_Display_Color_Set"));
        if (!create || !destroy_ || !countFn || !infoFn || !displaysFn || !capsFn || !get_ || !set_ || create(AdlAlloc, 1) != ADL_OK) {
            status_ = L"AMD: ADL initialization failed"; return false;
        }
        int count = 0; if (countFn(&count) != ADL_OK || count <= 0) { status_ = L"AMD: no active adapter"; return false; }
        auto* adapters = static_cast<AdapterInfo*>(std::calloc(static_cast<size_t>(count), sizeof(AdapterInfo)));
        if (!adapters || infoFn(adapters, sizeof(AdapterInfo) * count) != ADL_OK) { std::free(adapters); status_ = L"AMD: adapter enumeration failed"; return false; }
        std::string target(primaryDisplay.begin(), primaryDisplay.end());
        for (int i = 0; i < count && !initialized_; ++i) {
            int displayCount = 0; ADLDisplayInfo* displays = nullptr;
            if (displaysFn(adapters[i].iAdapterIndex, &displayCount, &displays, 0) != ADL_OK) continue;
            for (int j = 0; j < displayCount; ++j) {
                const int flags = displays[j].iDisplayInfoValue;
                if ((flags & (ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED)) !=
                    (ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED)) continue;
                if (_stricmp(displays[j].strDisplayName, target.c_str()) != 0) continue;
                int caps = 0, valid = 0; adapter_ = adapters[i].iAdapterIndex; display_ = displays[j].displayID.iDisplayLogicalIndex;
                if (capsFn(adapter_, display_, &caps, &valid) != ADL_OK || !(caps & valid & ADL_DISPLAY_COLOR_SATURATION)) continue;
                int def = 0, step = 0;
                if (get_(adapter_, display_, ADL_DISPLAY_COLOR_SATURATION, &original_, &def, &minimum_, &maximum_, &step) == ADL_OK && maximum_ > minimum_) initialized_ = true;
            }
            std::free(displays);
        }
        std::free(adapters);
        status_ = initialized_ ? L"AMD ADL saturation ready" : L"AMD: primary display saturation unsupported";
        return initialized_;
    }
    bool Apply(int normalized) override {
        if (!initialized_) return false; normalized = std::clamp(normalized, 0, 100);
        int native = minimum_ + ((maximum_ - minimum_) * normalized + 50) / 100;
        if (native == last_) return true;
        bool ok = set_(adapter_, display_, ADL_DISPLAY_COLOR_SATURATION, native) == ADL_OK;
        if (ok) last_ = native; else status_ = L"AMD: setting saturation failed"; return ok;
    }
    void RestoreOriginal() noexcept override { if (initialized_ && set_) { set_(adapter_, display_, ADL_DISPLAY_COLOR_SATURATION, original_); initialized_ = false; } }
    std::wstring Name() const override { return L"AMD ADL"; }
    std::wstring Status() const override { return status_; }
private:
    HMODULE module_{}; MainDestroy destroy_{}; ColorGet get_{}; ColorSet set_{};
    int adapter_{}, display_{}, original_{}, minimum_{}, maximum_{100}, last_{-1}; bool initialized_{}; std::wstring status_;
};
}
std::unique_ptr<GpuBackend> CreateAmdBackend() { return std::make_unique<AmdBackend>(); }


#include "gpu_backend.h"
#include <windows.h>
#include <algorithm>

namespace {
#ifndef NDEBUG
void NvTrace(const wchar_t* text) {
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp); std::wstring path = std::wstring(temp) + L"SilentVibrance-nv-trace.txt";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { DWORD bytes; WriteFile(file, text, static_cast<DWORD>(wcslen(text) * sizeof(wchar_t)), &bytes, nullptr); WriteFile(file, L"\r\n", 4, &bytes, nullptr); CloseHandle(file); }
}
#else
void NvTrace(const wchar_t*) {}
#endif
using NvStatus = int;
using NvDisplayHandle = void*;
using QueryInterface = void* (__cdecl*)(unsigned int);
using NvInitialize = NvStatus (__cdecl*)();
using Unload = NvStatus (__cdecl*)();
using GetDisplayHandle = NvStatus (__cdecl*)(const char*, NvDisplayHandle*);
using GetDvcInfo = NvStatus (__cdecl*)(NvDisplayHandle, unsigned int, void*);
using SetDvc = NvStatus (__cdecl*)(NvDisplayHandle, unsigned int, void*);

struct DvcInfo { unsigned int version; int current, minimum, maximum, defaultValue; };
constexpr unsigned int Version = sizeof(DvcInfo) | (1u << 16);
constexpr unsigned int IdInitialize = 0x0150e828, IdUnload = 0xd22bdd7e;
constexpr unsigned int IdGetDisplayHandle = 0x35c29134;
constexpr unsigned int IdGetDvcInfoEx = 0x0e45002d, IdSetDvcLevelEx = 0x4a82c2b1;

std::string NarrowDisplayName(const std::wstring& value) {
    int count = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(count ? count : 0, '\0');
    if (count > 1) WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, out.data(), count, nullptr, nullptr);
    if (!out.empty()) out.pop_back();
    return out;
}

class NvidiaBackend final : public GpuBackend {
public:
    ~NvidiaBackend() override { RestoreOriginal(); if (unload_) unload_(); if (module_) FreeLibrary(module_); }
    bool Initialize(const std::wstring& primaryDisplay) override {
        NvTrace(L"load");
        module_ = LoadLibraryW(L"nvapi64.dll");
        if (!module_) { status_ = L"NVIDIA: nvapi64.dll not present"; return false; }
        auto query = reinterpret_cast<QueryInterface>(GetProcAddress(module_, "nvapi_QueryInterface"));
        NvTrace(L"query export");
        if (!query) { status_ = L"NVIDIA: nvapi_QueryInterface missing"; return false; }
        auto initialize = reinterpret_cast<NvInitialize>(query(IdInitialize)); unload_ = reinterpret_cast<Unload>(query(IdUnload));
        NvTrace(L"query functions 1");
        getInfo_ = reinterpret_cast<GetDvcInfo>(query(IdGetDvcInfoEx)); set_ = reinterpret_cast<SetDvc>(query(IdSetDvcLevelEx));
        auto getHandle = reinterpret_cast<GetDisplayHandle>(query(IdGetDisplayHandle));
        NvTrace(L"query functions 2");
        if (!initialize || !getHandle || !getInfo_ || !set_ || initialize() != 0) {
            status_ = L"NVIDIA: driver does not expose DVC controls"; return false;
        }
        NvTrace(L"initialized");
        auto name = NarrowDisplayName(primaryDisplay);
        NvTrace(L"name narrowed");
        if (getHandle(name.c_str(), &display_) != 0 || !display_) {
            status_ = L"NVIDIA: primary display is not attached to the NVIDIA GPU"; return false;
        }
        NvTrace(L"display handle");
        DvcInfo info{}; info.version = Version;
        NvTrace(L"before get dvc");
        if (getInfo_(display_, 0, &info) != 0 || info.maximum <= info.minimum) {
            status_ = L"NVIDIA: DVC unsupported on primary display"; return false;
        }
        NvTrace(L"after get dvc");
        original_ = info.current; minimum_ = info.minimum; maximum_ = info.maximum; initialized_ = true;
        status_ = L"NVIDIA DVC ready"; return true;
    }
    bool Apply(int normalized) override {
        if (!initialized_) return false;
        normalized = std::clamp(normalized, 0, 100);
        const int native = minimum_ + ((maximum_ - minimum_) * normalized + 50) / 100;
        if (native == last_) return true;
        DvcInfo request{}; request.version = Version; request.current = native;
        const bool ok = set_(display_, 0, &request) == 0;
        if (ok) last_ = native; else status_ = L"NVIDIA: setting DVC failed";
        return ok;
    }
    void RestoreOriginal() noexcept override {
        if (initialized_ && set_) { DvcInfo request{}; request.version = Version; request.current = original_; set_(display_, 0, &request); initialized_ = false; }
    }
    std::wstring Name() const override { return L"NVIDIA DVC"; }
    std::wstring Status() const override { return status_; }
private:
    HMODULE module_{}; Unload unload_{}; GetDvcInfo getInfo_{}; SetDvc set_{}; NvDisplayHandle display_{};
    int original_{50}, minimum_{0}, maximum_{100}, last_{-1}; bool initialized_{}; std::wstring status_;
};
}

std::unique_ptr<GpuBackend> CreateNvidiaBackend() { return std::make_unique<NvidiaBackend>(); }

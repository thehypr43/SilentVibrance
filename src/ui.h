#pragma once
#include "config.h"
#include "process_monitor.h"
#include "gpu/gpu_backend.h"
#include <windows.h>
#include <commctrl.h>
#include <memory>
#include <vector>

class AppWindow {
public:
    explicit AppWindow(HINSTANCE instance);
    int Run(bool startHidden);
    void EmergencyRestore() noexcept;
    static constexpr wchar_t ClassName[] = L"SilentVibrance.Native.Window.2";
    static constexpr UINT ShowExistingMessage = WM_APP + 41;
private:
    bool Create(bool startHidden);
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
    void CreateControls();
    void RecreateFonts();
    void Layout();
    void Paint();
    void DrawOwnerItem(const DRAWITEMSTRUCT& item);
    void DrawListRow(const DRAWITEMSTRUCT& item);
    LRESULT DrawSlider(NMCUSTOMDRAW* custom);
    void RefreshList();
    void RefreshSelected();
    void ReleaseIcons();
    void ApplyForForeground();
    void Save();
    void AddRunning();
    void AddManual();
    void AddProfile(const std::wstring& name, const std::wstring& path);
    void RemoveSelected();
    void ReinitializeGpu();
    void SetStatus(const std::wstring& text);
    void UpdateAccent();
    void UpdateIcons();
    std::wstring ExePath() const;

    HINSTANCE instance_{}; HWND window_{}, titleLabel_{}, subtitleLabel_{}, defaultLabel_{}, defaultHint_{}, defaultSlider_{}, defaultValue_{}, applicationsLabel_{}, applicationsHint_{}, list_{}, selectedLabel_{}, selectedHint_{}, appSlider_{}, appValue_{}, startup_{}, status_{};
    HFONT font_{}, titleFont_{}, sectionFont_{}, smallFont_{}, microFont_{}, monoFont_{}; HBRUSH backgroundBrush_{}, cardBrush_{}, selectedBrush_{}, pillBrush_{}; UINT dpi_{96}; bool dark_{true}, exiting_{false}; int selected_{-1};
    COLORREF accent_{RGB(0,103,192)}, accentOn_{RGB(0,103,192)}; bool startupChecked_{false}, lastApplyOk_{true};
    std::vector<HICON> icons_;
    Settings settings_; ConfigStore config_; ForegroundMonitor monitor_; std::unique_ptr<GpuBackend> gpu_;
};

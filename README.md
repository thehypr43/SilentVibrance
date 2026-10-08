# Silent Vibrance

Tiny native Windows 10/11 x64 utility that switches display vibrance by foreground application. It has no console window, tray icon, service, updater, telemetry, network code, polling loop, database, or managed runtime.

## Architecture

- **C++20 + Win32/Common Controls:** compact executable, GUI-subsystem startup with no CMD window, native taskbar/minimize behavior, Per-Monitor V2 DPI awareness.
- **Layered native UI:** system light/dark theme, Windows 11 rounded cards, readable 10–20 pt DPI-scaled typography, high-contrast custom sliders without legacy tick marks, and zero animation/render loop.
- **Event-driven foreground detection:** `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` posts work to the UI message loop. Idle operation has no process-enumeration timer.
- **Display-aware GPU selection:** maps the primary GDI display (`\\.\DISPLAYx`) to its actual display adapter before loading a vendor backend. This avoids treating an NVIDIA rendering GPU as the display controller on Optimus systems.
- **NVIDIA:** loads the driver-supplied `nvapi64.dll` dynamically, maps the exact primary display with `NvAPI_GetAssociatedNvidiaDisplayHandle`, then uses the driver DVC entry points. NVIDIA's current public SDK does not document the DVC calls; the backend detects missing entry points and fails closed.
- **AMD:** loads the driver-supplied ADL library dynamically, matches the connected/mapped primary display, checks `ADL_DISPLAY_COLOR_SATURATION`, and uses AMD's documented `ADL_Display_Color_Get/Set` API.
- **Intel/other:** no public, supported Intel saturation-setting API was found. The utility reports unsupported hardware and makes no change rather than using registry/gamma hacks.
- **Persistence:** small per-user INI at `%LOCALAPPDATA%\SilentVibrance\settings.ini`.
- **Autostart:** per-user `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, with `--startup`; no elevation.

Sources used for backend decisions: [NVIDIA NVAPI SDK](https://github.com/NVIDIA/nvapi), [AMD ADL SDK](https://github.com/GPUOpen-LibrariesAndSDKs/display-library), [AMD ADLX display custom color API](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/main/SDK/Include/IDisplaySettings.h), and [Intel DDC/CI/API availability note](https://www.intel.com/content/www/us/en/support/articles/000007175/graphics.html).

## Behavior

- Slider range is normalized **0–100**. Each backend maps this into the exact native range returned by its driver.
- Foreground configured executable wins. An unconfigured foreground window restores the configured default.
- Default and per-app slider changes apply immediately and persist.
- **Add** lists visible running applications. **Add Manually** selects an `.exe`. **Remove** deletes the selected profile.
- Minimize uses normal taskbar behavior. Close **X** hides the window without exiting. **Exit** restores the launch-time driver value and terminates.
- A second launch signals the existing hidden window and exits.
- Startup launch stays hidden. Manual launch of the already-running instance shows it.
- Display/topology changes and sleep/resume trigger a delayed backend re-detection, avoiding driver hammering.
- Clean exit, logoff/shutdown, and unhandled exceptions attempt restoration. No user-mode app can guarantee restoration after forced termination, power loss, or process corruption.

## Build

Visual Studio 2022 / MSVC:

```powershell
cmake -S . -B build\msvc -A x64
cmake --build build\msvc --config Release
ctest --test-dir build\msvc -C Release --output-on-failure
```

Repository `scripts/build.ps1` also uses the available local CMake/Zig/Ninja toolchain:

```powershell
.\scripts\build.ps1 Release
.\scripts\test.ps1 Release
```

Output: `build\Release\SilentVibrance.exe`. Task Manager identifies it as **Silent Vibrance**.

## Project layout

```text
src/
  main.cpp                 single instance and process lifetime
  ui.cpp/.h                native window and controls
  model.cpp/.h             profile selection
  config.cpp/.h            INI and per-user startup
  process_monitor.cpp/.h   WinEvent foreground hook and Add list
  gpu/                     backend interface, NVIDIA, AMD, Intel
third_party/amd_adl/       AMD's official headers and license
resources/                 manifest and resources
tests/                     model/config tests
scripts/                   build/test entry points
```

## Current hardware scope

The first release controls the primary active display only. Backend interfaces and display identity are separated so per-monitor selection can be added without UI/config rewrites. It never iterates every connected output and blindly applies a setting.

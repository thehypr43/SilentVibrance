#include "ui.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <filesystem>
#include <algorithm>
#include <objidl.h>
using std::min; using std::max;
#include <gdiplus.h>

namespace {
enum ControlId { ID_DEFAULT = 100, ID_ADD, ID_ADD_MANUAL, ID_REMOVE, ID_LIST, ID_APP_SLIDER, ID_STARTUP, ID_EXIT, ID_DEFAULT_VALUE, ID_APP_VALUE };
constexpr UINT WM_FOREGROUND_CHANGED = WM_APP + 10;
constexpr UINT WM_ACCENT_CHANGED = 0x0320; // WM_DWMCOLORIZATIONCOLORCHANGED
constexpr UINT_PTR TIMER_GPU_REINIT = 7;

// Fluent design tokens taken from the Windows 11 handoff mock-up.
constexpr COLORREF kAccentLight = RGB(0, 103, 192);   // #0067C0
constexpr COLORREF kAccentDark = RGB(96, 205, 255);   // #60CDFF
constexpr COLORREF kOnAccentDark = RGB(0, 40, 74);    // #00284A

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
std::wstring FileName(const std::wstring& path) { return std::filesystem::path(path).filename().wstring(); }
std::wstring Stem(const std::wstring& path) { return std::filesystem::path(path).stem().wstring(); }
bool DarkModeEnabled() {
    DWORD light=1,size=sizeof(light); HKEY key{};
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS){
        RegQueryValueExW(key,L"AppsUseLightTheme",nullptr,nullptr,reinterpret_cast<BYTE*>(&light),&size);RegCloseKey(key);
    }
    return light==0;
}
COLORREF Mix(COLORREF a,COLORREF b,int amount){return RGB((GetRValue(a)*(100-amount)+GetRValue(b)*amount)/100,(GetGValue(a)*(100-amount)+GetGValue(b)*amount)/100,(GetBValue(a)*(100-amount)+GetBValue(b)*amount)/100);}
Gdiplus::Color Argb(COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
// radius is a CSS-style corner radius; it is clamped so large values give a full pill.
void RoundPath(Gdiplus::GraphicsPath& path,Gdiplus::REAL x,Gdiplus::REAL y,Gdiplus::REAL w,Gdiplus::REAL h,Gdiplus::REAL radius){
    const Gdiplus::REAL d=std::min({radius*2,w,h});
    if(d<=0){path.AddRectangle(Gdiplus::RectF(x,y,w,h));return;}
    path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
}
void Smooth(Gdiplus::Graphics& g){g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias8x8);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);}
void FillRound(HDC dc,RECT r,int radius,COLORREF color){
    Gdiplus::Graphics g(dc);Smooth(g);Gdiplus::GraphicsPath path;
    RoundPath(path,Gdiplus::REAL(r.left),Gdiplus::REAL(r.top),Gdiplus::REAL(r.right-r.left),Gdiplus::REAL(r.bottom-r.top),Gdiplus::REAL(radius));
    Gdiplus::SolidBrush brush(Argb(color));g.FillPath(&brush,&path);
}
void StrokeRound(HDC dc,RECT r,int radius,COLORREF color){
    Gdiplus::Graphics g(dc);Smooth(g);Gdiplus::GraphicsPath path;
    RoundPath(path,r.left+0.5f,r.top+0.5f,Gdiplus::REAL(r.right-r.left-1),Gdiplus::REAL(r.bottom-r.top-1),Gdiplus::REAL(radius)-0.5f);
    Gdiplus::Pen pen(Argb(color),1.0f);g.DrawPath(&pen,&path);
}
enum class Glyph { Half, Grid, Diamond };
// Vector chip icons; the font glyphs fell back to blocky symbol fonts.
void DrawGlyph(HDC dc,RECT box,Glyph glyph,COLORREF color){
    Gdiplus::Graphics g(dc);Smooth(g);Gdiplus::SolidBrush brush(Argb(color));
    const Gdiplus::REAL cx=(box.left+box.right)/2.0f,cy=(box.top+box.bottom)/2.0f,u=(box.right-box.left)/34.0f;
    if(glyph==Glyph::Half){
        const Gdiplus::REAL r=7*u;Gdiplus::Pen pen(Argb(color),1.6f*u);
        g.DrawEllipse(&pen,cx-r,cy-r,2*r,2*r);g.FillPie(&brush,cx-r,cy-r,2*r,2*r,90,180);
    }else if(glyph==Glyph::Grid){
        const Gdiplus::REAL cell=5.5f*u,gap=2*u,start=-(cell+gap/2);
        for(int i=0;i<2;++i)for(int j=0;j<2;++j){Gdiplus::GraphicsPath path;RoundPath(path,cx+start+i*(cell+gap),cy+start+j*(cell+gap),cell,cell,1.2f*u);g.FillPath(&brush,&path);}
    }else{
        const Gdiplus::REAL r=7.5f*u,in=3.5f*u;Gdiplus::Pen pen(Argb(color),1.6f*u);pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF outer[]{{cx,cy-r},{cx+r,cy},{cx,cy+r},{cx-r,cy}};g.DrawPolygon(&pen,outer,4);
        Gdiplus::PointF inner[]{{cx,cy-in},{cx+in,cy},{cx,cy+in},{cx-in,cy}};g.FillPolygon(&brush,inner,4);
    }
}

// Single source of truth for the vertical rhythm, shared by Layout() and Paint().
struct Metrics {
    int margin, pad, chip, width, height;
    int headerCardTop, headerCardBottom;   // "Desktop vibrance"
    int listCardTop, listCardBottom;       // "Applications"
    int profileCardTop, profileCardBottom; // selected profile
    int footerTop;
};
Metrics Measure(const RECT& client, UINT dpi) {
    auto s=[&](int v){return Scale(v,dpi);};
    Metrics m{};
    m.margin=s(28); m.pad=s(18); m.chip=s(34);
    m.width=client.right-client.left; m.height=client.bottom-client.top;
    m.headerCardTop=s(100); m.headerCardBottom=m.headerCardTop+s(66);
    m.footerTop=m.height-s(62);
    m.profileCardBottom=m.footerTop-s(18); m.profileCardTop=m.profileCardBottom-s(66);
    m.listCardTop=m.headerCardBottom+s(14); m.listCardBottom=m.profileCardTop-s(14);
    return m;
}

#ifndef NDEBUG
void Trace(const wchar_t* text) {
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp); std::wstring path = std::wstring(temp) + L"SilentVibrance-trace.txt";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { DWORD bytes; WriteFile(file, text, static_cast<DWORD>(wcslen(text) * sizeof(wchar_t)), &bytes, nullptr); WriteFile(file, L"\r\n", 4, &bytes, nullptr); CloseHandle(file); }
}
#else
void Trace(const wchar_t*) {}
#endif
}

AppWindow::AppWindow(HINSTANCE instance) : instance_(instance) {}

int AppWindow::Run(bool startHidden) {
    std::wstring error; config_.Load(settings_, error);
    Gdiplus::GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken=0; Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr);
    struct GdiplusScope { ULONG_PTR token; ~GdiplusScope(){ Gdiplus::GdiplusShutdown(token); } } gdiplusScope{gdiplusToken};
    if (!Create(startHidden)) return 2;
    monitor_.Start([this]{ if (window_) PostMessageW(window_, WM_FOREGROUND_CHANGED, 0, 0); });
    ApplyForForeground();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return static_cast<int>(message.wParam);
}

bool AppWindow::Create(bool startHidden) {
    Trace(L"Create begin");
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc; wc.hInstance = instance_; wc.lpszClassName = ClassName;
    HICON appIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = appIcon ? appIcon : LoadIcon(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window_ = CreateWindowExW(0, ClassName, L"Silent Vibrance", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 680,
                              nullptr, nullptr, instance_, this);
    Trace(L"CreateWindow returned");
    if (!window_) return false;
    dark_=DarkModeEnabled(); UpdateAccent(); const BOOL dark=dark_; DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
    const COLORREF caption=dark_?RGB(32,32,34):RGB(243,243,245); DwmSetWindowAttribute(window_,35,&caption,sizeof(caption)); // DWMWA_CAPTION_COLOR
    UpdateIcons();
    const int rounded=2; DwmSetWindowAttribute(window_,33,&rounded,sizeof(rounded));
    ReinitializeGpu();
    Trace(L"GPU initialized");
    ShowWindow(window_, startHidden ? SW_HIDE : SW_SHOWNORMAL);
    const UINT windowDpi=GetDpiForWindow(window_);RECT desired{0,0,Scale(716,windowDpi),Scale(618,windowDpi)};AdjustWindowRectExForDpi(&desired,WS_OVERLAPPEDWINDOW,FALSE,0,windowDpi);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTOPRIMARY),&monitor);const int width=desired.right-desired.left,height=desired.bottom-desired.top,x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
    SetWindowPos(window_,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    if (!startHidden) SetForegroundWindow(window_);
    return true;
}

LRESULT CALLBACK AppWindow::WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<AppWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wp, lp) : DefWindowProcW(window, message, wp, lp);
}

void AppWindow::CreateControls() {
    Trace(L"CreateControls begin");
    dpi_=GetDpiForWindow(window_);
    dark_=DarkModeEnabled(); UpdateAccent(); // brushes below depend on both
    RecreateFonts();
    const COLORREF card=dark_?RGB(43,43,45):RGB(255,255,255);
    backgroundBrush_=CreateSolidBrush(dark_?RGB(32,32,34):RGB(243,243,245));
    cardBrush_=CreateSolidBrush(card);
    selectedBrush_=CreateSolidBrush(Mix(card,accentOn_,dark_?12:8));
    pillBrush_=CreateSolidBrush(dark_?RGB(49,49,51):RGB(250,250,251));
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0,0,0,0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE); return h;
    };
    titleLabel_=make(L"STATIC",L"Silent Vibrance",SS_LEFT,0);SendMessageW(titleLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont_),TRUE);
    subtitleLabel_=make(L"STATIC",L"Color profiles that follow your active application",SS_LEFT,0);SendMessageW(subtitleLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont_),TRUE);
    defaultLabel_ = make(L"STATIC", L"Desktop vibrance", SS_LEFT, 0);SendMessageW(defaultLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(sectionFont_),TRUE);
    defaultHint_=make(L"STATIC",L"Used when no configured app is focused",SS_LEFT|SS_ENDELLIPSIS,0);SendMessageW(defaultHint_,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont_),TRUE);
    defaultSlider_ = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_DEFAULT);
    SendMessageW(defaultSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); SendMessageW(defaultSlider_, TBM_SETPOS, TRUE, settings_.defaultVibrance);
    defaultValue_ = make(L"STATIC", std::to_wstring(settings_.defaultVibrance).c_str(), SS_OWNERDRAW, ID_DEFAULT_VALUE);
    make(L"BUTTON", L"+ Add running", BS_OWNERDRAW, ID_ADD); make(L"BUTTON", L"Browse…", BS_OWNERDRAW, ID_ADD_MANUAL); make(L"BUTTON", L"Remove", BS_OWNERDRAW, ID_REMOVE);
    applicationsLabel_ = make(L"STATIC", L"Applications", SS_LEFT, 0);SendMessageW(applicationsLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(sectionFont_),TRUE);
    applicationsHint_=make(L"STATIC",L"Foreground application has priority",SS_LEFT|SS_ENDELLIPSIS,0);SendMessageW(applicationsHint_,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont_),TRUE);
    list_ = make(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDRAWFIXED | LVS_NOCOLUMNHEADER | WS_TABSTOP, ID_LIST);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    if (HIMAGELIST rowHeight = ImageList_Create(1, Scale(40, dpi_), ILC_COLOR32, 1, 1)) ListView_SetImageList(list_, rowHeight, LVSIL_SMALL);
    const wchar_t* columns[] = {L"Executable", L"Path", L"Vibrance"}; int widths[] = {179, 300, 84};
    for (int i = 0; i < 3; ++i) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = const_cast<wchar_t*>(columns[i]); c.cx = Scale(widths[i], dpi_); ListView_InsertColumn(list_, i, &c); }
    selectedLabel_ = make(L"STATIC", L"No profile selected", SS_LEFT|SS_ENDELLIPSIS, 0);SendMessageW(selectedLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(sectionFont_),TRUE);
    selectedHint_=make(L"STATIC",L"Choose an application above to edit its vibrance",SS_LEFT|SS_ENDELLIPSIS,0);SendMessageW(selectedHint_,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont_),TRUE);
    appSlider_ = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, ID_APP_SLIDER); SendMessageW(appSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    appValue_ = make(L"STATIC", L"-", SS_OWNERDRAW, ID_APP_VALUE);
    startup_ = make(L"BUTTON", L"Start with Windows", BS_OWNERDRAW | WS_TABSTOP, ID_STARTUP); startupChecked_ = settings_.startWithWindows;
    status_ = make(L"STATIC", L"Initializing display backend...", SS_LEFT|SS_ENDELLIPSIS, 0);ShowWindow(status_,SW_HIDE);
    make(L"BUTTON", L"Exit", BS_OWNERDRAW, ID_EXIT);
    const wchar_t* theme=dark_?L"DarkMode_Explorer":L"Explorer";
    for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) SetWindowTheme(child,theme,nullptr);
    ListView_SetBkColor(list_,card);ListView_SetTextBkColor(list_,card);ListView_SetTextColor(list_,dark_?RGB(255,255,255):RGB(27,27,27));
    RefreshList(); RefreshSelected();
    Trace(L"CreateControls end");
}

void AppWindow::RecreateFonts() {
    // The mock-up specifies pixel sizes, so build the fonts straight in device pixels.
    auto px=[&](int value){return -Scale(value,dpi_);};
    LOGFONTW base{};wcscpy_s(base.lfFaceName,L"Segoe UI Variable Text");base.lfHeight=px(14);base.lfWeight=FW_NORMAL;base.lfQuality=CLEARTYPE_QUALITY;
    LOGFONTW title=base;wcscpy_s(title.lfFaceName,L"Segoe UI Variable Display");title.lfHeight=px(30);title.lfWeight=FW_SEMIBOLD;
    LOGFONTW section=base;section.lfHeight=px(14);section.lfWeight=FW_SEMIBOLD;
    LOGFONTW small=base;small.lfHeight=px(13);
    LOGFONTW micro=base;micro.lfHeight=px(11);micro.lfWeight=FW_SEMIBOLD;
    LOGFONTW mono=base;wcscpy_s(mono.lfFaceName,L"Consolas");mono.lfHeight=px(13);mono.lfWeight=FW_SEMIBOLD;
    HFONT nextFont=CreateFontIndirectW(&base),nextTitle=CreateFontIndirectW(&title),nextSection=CreateFontIndirectW(&section),nextSmall=CreateFontIndirectW(&small),nextMicro=CreateFontIndirectW(&micro),nextMono=CreateFontIndirectW(&mono);
    HFONT oldFont=font_,oldTitle=titleFont_,oldSection=sectionFont_,oldSmall=smallFont_,oldMicro=microFont_,oldMono=monoFont_;
    font_=nextFont;titleFont_=nextTitle;sectionFont_=nextSection;smallFont_=nextSmall;microFont_=nextMicro;monoFont_=nextMono;
    if(window_){for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(titleLabel_)SendMessageW(titleLabel_,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont_),TRUE);for(HWND section2:{defaultLabel_,applicationsLabel_,selectedLabel_})if(section2)SendMessageW(section2,WM_SETFONT,reinterpret_cast<WPARAM>(sectionFont_),TRUE);for(HWND hint:{subtitleLabel_,defaultHint_,applicationsHint_,selectedHint_,status_})if(hint)SendMessageW(hint,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont_),TRUE);}
    if(oldFont)DeleteObject(oldFont);if(oldTitle)DeleteObject(oldTitle);if(oldSection)DeleteObject(oldSection);if(oldSmall)DeleteObject(oldSmall);if(oldMicro)DeleteObject(oldMicro);if(oldMono)DeleteObject(oldMono);
}

void AppWindow::Layout() {
    RECT client{}; GetClientRect(window_, &client); dpi_=GetDpiForWindow(window_);
    const Metrics g=Measure(client,dpi_); auto s=[&](int v){return Scale(v,dpi_);};
    auto item=[&](int id){ return GetDlgItem(window_,id); };
    const int textLeft=g.margin+g.pad+g.chip+s(16), contentRight=g.width-g.margin-g.pad;
    const int pillW=s(258);

    MoveWindow(titleLabel_,g.margin+s(2),s(20),g.width-g.margin-pillW-s(24),s(40),TRUE);
    MoveWindow(subtitleLabel_,g.margin+s(2),s(60),g.width-g.margin-pillW-s(24),s(20),TRUE);

    // A vibrance row is: chip | title + hint | slider | value box, right aligned.
    // Labels stop before the controls on their right; otherwise their background paints over them.
    auto vibranceRow=[&](int top,HWND label,HWND hint,HWND slider,HWND value){
        const int valueX=contentRight-s(56),sliderX=valueX-s(16)-s(170),textW=std::max(s(40),sliderX-s(12)-textLeft);
        MoveWindow(label,textLeft,top+s(14),textW,s(20),TRUE);
        MoveWindow(hint,textLeft,top+s(35),textW,s(18),TRUE);
        MoveWindow(value,valueX,top+s(16),s(56),s(34),TRUE);
        MoveWindow(slider,sliderX,top+s(16),s(170),s(34),TRUE);
    };
    vibranceRow(g.headerCardTop,defaultLabel_,defaultHint_,defaultSlider_,defaultValue_);
    vibranceRow(g.profileCardTop,selectedLabel_,selectedHint_,appSlider_,appValue_);

    const int buttonY=g.listCardTop+s(14),buttonH=s(32);
    const int removeX=contentRight-s(80),browseX=removeX-s(8)-s(86),addX=browseX-s(8)-s(120);
    MoveWindow(applicationsLabel_,textLeft,g.listCardTop+s(13),std::max(s(40),addX-s(12)-textLeft),s(20),TRUE);
    MoveWindow(applicationsHint_,textLeft,g.listCardTop+s(34),std::max(s(40),addX-s(12)-textLeft),s(18),TRUE);
    MoveWindow(item(ID_ADD),addX,buttonY,s(120),buttonH,TRUE);
    MoveWindow(item(ID_ADD_MANUAL),browseX,buttonY,s(86),buttonH,TRUE);
    MoveWindow(item(ID_REMOVE),removeX,buttonY,s(80),buttonH,TRUE);

    const int listX=g.margin+s(10),listW=g.width-2*g.margin-s(20),listY=g.listCardTop+s(82);
    MoveWindow(list_,listX,listY,listW,std::max(s(80),g.listCardBottom-s(12)-listY),TRUE);
    {const int exe=s(179),vibrance=s(84);ListView_SetColumnWidth(list_,0,exe);ListView_SetColumnWidth(list_,1,std::max(s(80),listW-exe-vibrance));ListView_SetColumnWidth(list_,2,vibrance);}

    MoveWindow(startup_,g.margin+s(4),g.footerTop,s(220),s(36),TRUE);
    MoveWindow(item(ID_EXIT),g.width-g.margin-s(4)-s(88),g.footerTop,s(88),s(36),TRUE);
    InvalidateRect(window_,nullptr,FALSE);
}

void AppWindow::Paint(){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(window_,&ps);RECT client{};GetClientRect(window_,&client);
    const Metrics g=Measure(client,dpi_); auto s=[&](int v){return Scale(v,dpi_);};
    const COLORREF card=dark_?RGB(43,43,45):RGB(255,255,255),border=dark_?RGB(59,59,61):RGB(233,233,236),accent=accentOn_;
    FillRect(dc,&client,backgroundBrush_);
    const int radius=s(8);

    auto cardBox=[&](int top,int bottom){RECT r{g.margin,top,g.width-g.margin,bottom};FillRound(dc,r,radius,card);StrokeRound(dc,r,radius,border);};
    cardBox(g.headerCardTop,g.headerCardBottom);
    cardBox(g.listCardTop,g.listCardBottom);
    RECT profile{g.margin,g.profileCardTop,g.width-g.margin,g.profileCardBottom};
    FillRound(dc,profile,radius,Mix(card,accent,dark_?12:8));StrokeRound(dc,profile,radius,Mix(border,accent,50));

    auto chip=[&](int top,Glyph glyph,COLORREF fg,COLORREF bg){
        RECT r{g.margin+g.pad,top+s(16),g.margin+g.pad+g.chip,top+s(16)+g.chip};
        FillRound(dc,r,s(7),bg);DrawGlyph(dc,r,glyph,fg);};
    const COLORREF chipBg=Mix(card,accent,dark_?14:10);
    chip(g.headerCardTop,Glyph::Half,accent,chipBg);
    chip(g.listCardTop-s(2),Glyph::Grid,accent,chipBg);
    chip(g.profileCardTop,Glyph::Diamond,dark_?kOnAccentDark:RGB(255,255,255),accent);

    // Column labels sit between the card header and the rows, like the mock-up.
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,dark_?RGB(169,169,169):RGB(107,107,107));
    {auto oldFont=SelectObject(dc,microFont_);
     const int labelTop=g.listCardTop+s(60),labelBottom=labelTop+s(18),rowLeft=g.margin+s(10);
     RECT exeLabel{rowLeft+s(29),labelTop,rowLeft+s(179),labelBottom};
     DrawTextW(dc,L"EXECUTABLE",-1,&exeLabel,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     RECT pathLabel{rowLeft+s(193),labelTop,g.width-g.margin-s(10)-s(100),labelBottom};
     DrawTextW(dc,L"PATH",-1,&pathLabel,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     RECT vibranceLabel{pathLabel.right,labelTop,g.width-g.margin-s(10)-s(12),labelBottom};
     DrawTextW(dc,L"VIBRANCE",-1,&vibranceLabel,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
     SelectObject(dc,oldFont);}

    const int pillW=s(258),pillH=s(28),pillX=g.width-g.margin-pillW,pillY=s(52);
    RECT pillRect{pillX,pillY,pillX+pillW,pillY+pillH};
    FillRound(dc,pillRect,pillH/2,dark_?RGB(49,49,51):RGB(250,250,251));StrokeRound(dc,pillRect,pillH/2,border);
    const int dotR=s(4),dotCy=pillY+pillH/2,dotCx=pillX+Scale(15,dpi_);
    RECT dot{dotCx-dotR,dotCy-dotR,dotCx+dotR,dotCy+dotR};FillRound(dc,dot,dotR,lastApplyOk_?(dark_?RGB(108,203,95):RGB(15,123,15)):RGB(196,43,28));
    // Status text is painted here; the static stays hidden (its text is still read by the lifecycle test).
    wchar_t statusText[512]{};GetWindowTextW(status_,statusText,512);
    RECT statusRect{pillX+s(28),pillY,pillX+pillW-s(12),pillY+pillH};
    SetTextColor(dc,dark_?RGB(197,197,197):RGB(68,68,68));
    {auto oldFont=SelectObject(dc,smallFont_);DrawTextW(dc,statusText,-1,&statusRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,oldFont);}
    EndPaint(window_,&ps);
}

void AppWindow::DrawListRow(const DRAWITEMSTRUCT& item){
    HDC dc=item.hDC;RECT row=item.rcItem;const int index=static_cast<int>(item.itemID);
    if(index<0||index>=static_cast<int>(settings_.applications.size()))return;
    const auto& profile=settings_.applications[index];
    const bool selected=ListView_GetItemState(list_,index,LVIS_SELECTED)!=0;
    auto s=[&](int v){return Scale(v,dpi_);};
    const COLORREF card=dark_?RGB(43,43,45):RGB(255,255,255),sub=dark_?RGB(169,169,169):RGB(93,93,93),accent=accentOn_;
    SetBkMode(dc,TRANSPARENT);
    HBRUSH bg=CreateSolidBrush(card);FillRect(dc,&row,bg);DeleteObject(bg);
    RECT pillRow=row;InflateRect(&pillRow,-s(2),0);pillRow.top+=s(1);pillRow.bottom-=s(1);
    if(selected)FillRound(dc,pillRow,s(6),Mix(card,accent,dark_?14:10));

    const int cy=(row.top+row.bottom)/2;
    RECT mark{row.left+s(12),cy-s(9),row.left+s(12)+s(3),cy+s(9)};
    if(selected)FillRound(dc,mark,s(3),accent);

    int x=row.left+s(29);
    RECT icon{x,cy-s(11),x+s(22),cy+s(11)};
    if(index<static_cast<int>(icons_.size())&&icons_[index])
        DrawIconEx(dc,icon.left,icon.top,icons_[index],s(22),s(22),0,nullptr,DI_NORMAL);
    else
        FillRound(dc,icon,s(5),Mix(card,dark_?RGB(255,255,255):RGB(0,0,0),8));

    RECT exeRect{x+s(32),row.top,row.left+s(179)-s(6),row.bottom};
    SetTextColor(dc,dark_?RGB(255,255,255):RGB(27,27,27));
    auto oldFont=SelectObject(dc,monoFont_);
    DrawTextW(dc,profile.executable.c_str(),-1,&exeRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,oldFont);

    const int pillW=s(52),pillH=s(22),pillX=row.right-s(12)-pillW;
    RECT pathRect{row.left+s(193),row.top,pillX-s(14),row.bottom};
    SetTextColor(dc,sub);oldFont=SelectObject(dc,smallFont_);
    DrawTextW(dc,profile.path.c_str(),-1,&pathRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_PATH_ELLIPSIS);

    RECT badge{pillX,cy-pillH/2,pillX+pillW,cy+pillH/2};
    FillRound(dc,badge,pillH,Mix(card,accent,dark_?18:12));
    SetTextColor(dc,dark_?Mix(accent,RGB(255,255,255),25):Mix(accent,RGB(0,0,0),20));
    SelectObject(dc,microFont_);
    DrawTextW(dc,std::to_wstring(profile.vibrance).c_str(),-1,&badge,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,oldFont);
}

void AppWindow::DrawOwnerItem(const DRAWITEMSTRUCT& item){
    if(item.CtlID==ID_LIST){ DrawListRow(item); return; }
    HDC dc=item.hDC;RECT rect=item.rcItem;const bool pressed=(item.itemState&ODS_SELECTED)!=0,disabled=(item.itemState&ODS_DISABLED)!=0;SetBkMode(dc,TRANSPARENT);
    auto s=[&](int v){return Scale(v,dpi_);};
    const COLORREF card=dark_?RGB(43,43,45):RGB(255,255,255);
    if(item.CtlID==ID_DEFAULT_VALUE||item.CtlID==ID_APP_VALUE){
        const bool onProfileCard=item.CtlID==ID_APP_VALUE;
        FillRect(dc,&rect,onProfileCard?selectedBrush_:cardBrush_);
        COLORREF fill=dark_?Mix(card,RGB(255,255,255),6):RGB(253,253,253);
        FillRound(dc,rect,s(6),fill);StrokeRound(dc,rect,s(6),dark_?RGB(68,68,70):RGB(230,230,230));
        wchar_t value[16]{};GetWindowTextW(item.hwndItem,value,16);
        SetTextColor(dc,dark_?RGB(255,255,255):RGB(27,27,27));auto old=SelectObject(dc,sectionFont_);
        DrawTextW(dc,value,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);return;
    }
    if(item.CtlID==ID_STARTUP){
        FillRect(dc,&rect,backgroundBrush_);
        const int trackW=s(40),trackH=s(20),ty=(rect.top+rect.bottom)/2-trackH/2;
        RECT track{rect.left,ty,rect.left+trackW,ty+trackH};
        if(startupChecked_)FillRound(dc,track,trackH,accentOn_);
        else{FillRound(dc,track,trackH,dark_?RGB(32,32,34):RGB(243,243,245));StrokeRound(dc,track,trackH,dark_?RGB(154,154,154):RGB(138,138,138));}
        const int knob=trackH-s(4),kx=startupChecked_?track.right-knob-s(2):track.left+s(2),ky=ty+s(2);
        RECT knobRect{kx,ky,kx+knob,ky+knob};
        FillRound(dc,knobRect,knob,startupChecked_?(dark_?kOnAccentDark:RGB(255,255,255)):(dark_?RGB(208,208,208):RGB(90,90,90)));
        RECT textRect{track.right+s(11),rect.top,rect.right,rect.bottom};wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);
        SetTextColor(dc,dark_?RGB(255,255,255):RGB(27,27,27));auto old=SelectObject(dc,font_);DrawTextW(dc,label,-1,&textRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);
        if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){RECT focus=track;InflateRect(&focus,s(3),s(3));DrawFocusRect(dc,&focus);}
        return;
    }
    FillRect(dc,&rect,item.CtlID==ID_EXIT?backgroundBrush_:cardBrush_);
    COLORREF fill,border,text;
    if(item.CtlID==ID_ADD){fill=accentOn_;border=fill;text=dark_?kOnAccentDark:RGB(255,255,255);}
    else{fill=dark_?RGB(61,61,63):RGB(253,253,253);border=dark_?RGB(74,74,76):RGB(230,230,230);
         text=item.CtlID==ID_REMOVE?(dark_?RGB(255,153,164):RGB(164,38,44)):(dark_?RGB(255,255,255):RGB(27,27,27));}
    if(pressed)fill=Mix(fill,dark_?RGB(255,255,255):RGB(0,0,0),12);
    if(disabled)text=dark_?RGB(120,120,124):RGB(160,160,164);
    FillRound(dc,rect,s(5),fill);if(item.CtlID!=ID_ADD)StrokeRound(dc,rect,s(5),border);
    wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);SetTextColor(dc,text);
    auto old=SelectObject(dc,item.CtlID==ID_ADD?sectionFont_:font_);
    DrawTextW(dc,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);
    if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){RECT focus=rect;InflateRect(&focus,-s(4),-s(4));DrawFocusRect(dc,&focus);}
}

LRESULT AppWindow::DrawSlider(NMCUSTOMDRAW* custom){
    if(custom->dwDrawStage!=CDDS_PREPAINT)return CDRF_SKIPDEFAULT;
    HWND slider=custom->hdr.hwndFrom;HDC dc=custom->hdc;auto s=[&](int v){return Scale(v,dpi_);};
    const bool onProfileCard=slider==appSlider_,enabled=IsWindowEnabled(slider)!=FALSE;
    const COLORREF surface=onProfileCard?Mix(dark_?RGB(43,43,45):RGB(255,255,255),accentOn_,dark_?12:8):(dark_?RGB(43,43,45):RGB(255,255,255));
    const COLORREF track=dark_?RGB(106,106,108):RGB(201,201,201),accent=accentOn_;
    RECT client{};GetClientRect(slider,&client);
    FillRect(dc,&client,onProfileCard?selectedBrush_:cardBrush_);

    const int thumb=s(20),cy=(client.top+client.bottom)/2,left=client.left+thumb/2,right=client.right-thumb/2;
    const int pos=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0)),cx=left+MulDiv(right-left,std::clamp(pos,0,100),100);
    const int thickness=s(4);
    RECT base{left,cy-thickness/2,right,cy+(thickness+1)/2};FillRound(dc,base,thickness,track);
    RECT active=base;active.right=cx;if(active.right>active.left)FillRound(dc,active,thickness,enabled?accent:Mix(accent,surface,60));
    RECT knob{cx-thumb/2,cy-thumb/2,cx+(thumb+1)/2,cy+(thumb+1)/2};
    FillRound(dc,knob,thumb,dark_?RGB(69,69,69):RGB(255,255,255));
    StrokeRound(dc,knob,thumb,dark_?RGB(24,24,26):RGB(214,214,214));
    RECT core=knob;InflateRect(&core,-s(5),-s(5));FillRound(dc,core,thumb,enabled?accent:Mix(accent,surface,60));
    return CDRF_SKIPDEFAULT;
}

void AppWindow::ReleaseIcons(){ for(HICON icon:icons_) if(icon) DestroyIcon(icon); icons_.clear(); }

void AppWindow::RefreshList() {
    ListView_DeleteAllItems(list_); ReleaseIcons();
    for (int i=0;i<static_cast<int>(settings_.applications.size());++i) {
        auto& p=settings_.applications[i]; LVITEMW it{}; it.mask=LVIF_TEXT; it.iItem=i; it.pszText=p.executable.data(); ListView_InsertItem(list_,&it);
        // Extract at the exact drawn size instead of stretching a 16px icon.
        HICON icon=nullptr; const UINT size=static_cast<UINT>(Scale(22,dpi_));
        if(PrivateExtractIconsW(p.path.c_str(),0,static_cast<int>(size),static_cast<int>(size),&icon,nullptr,1,0)!=1||!icon){ icon=nullptr; SHFILEINFOW info{}; if(SHGetFileInfoW(p.path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)) icon=info.hIcon; }
        icons_.push_back(icon);
    }
    if (selected_ >= static_cast<int>(settings_.applications.size())) selected_=-1;
}
void AppWindow::RefreshSelected() {
    const bool valid=selected_>=0 && selected_<static_cast<int>(settings_.applications.size()); EnableWindow(appSlider_,valid);
    if(valid){ auto& profile=settings_.applications[selected_];int v=profile.vibrance; SendMessageW(appSlider_,TBM_SETPOS,TRUE,v); SetWindowTextW(appValue_,std::to_wstring(v).c_str());
        SetWindowTextW(selectedLabel_,(profile.name+L" profile").c_str());
        SetWindowTextW(selectedHint_,L"Profile applies while this app is in the foreground"); }
    else{SetWindowTextW(appValue_,L"-");SetWindowTextW(selectedLabel_,L"No profile selected");SetWindowTextW(selectedHint_,L"Choose an application above to edit its vibrance");}
    RedrawWindow(appSlider_,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
}

void AppWindow::ApplyForForeground() {
    if (!gpu_) return; const auto exe=ForegroundMonitor::ForegroundExecutable(); const int found=FindProfileForExecutable(settings_,exe);
    const int value=found>=0?settings_.applications[found].vibrance:settings_.defaultVibrance;
    if (!gpu_->Apply(value)) { lastApplyOk_=false; SetStatus(gpu_->Status()); }
    else { lastApplyOk_=true; SetStatus(gpu_->Name()+L" active · "+std::to_wstring(value)+L" ("+(found>=0?settings_.applications[found].executable:L"default")+L")"); }
    if(window_) InvalidateRect(window_,nullptr,FALSE);
}

void AppWindow::UpdateIcons() {
    // Load the exact frame for this DPI; letting Windows shrink the 32px frame looks pixelated.
    const UINT dpi=GetDpiForWindow(window_);
    auto load=[&](int metric){const int size=GetSystemMetricsForDpi(metric,dpi);return static_cast<HICON>(LoadImageW(instance_,MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));};
    if(HICON old=reinterpret_cast<HICON>(SendMessageW(window_,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(load(SM_CXSMICON)))))DestroyIcon(old);
    if(HICON old=reinterpret_cast<HICON>(SendMessageW(window_,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(load(SM_CXICON)))))DestroyIcon(old);
}

void AppWindow::UpdateAccent() { accent_=dark_?kAccentDark:kAccentLight; accentOn_=accent_; }

void AppWindow::Save() { std::wstring error; if(!config_.Save(settings_,error)) SetStatus(error); }
void AppWindow::SetStatus(const std::wstring& text) { if(status_) SetWindowTextW(status_,text.c_str()); if(window_) InvalidateRect(window_,nullptr,FALSE); }
std::wstring AppWindow::ExePath() const { std::wstring p(32768,L'\0'); DWORD n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size())); p.resize(n); return p; }

void AppWindow::AddProfile(const std::wstring& name,const std::wstring& path){
    const auto exe=FileName(path); int existing=FindProfileForExecutable(settings_,exe);
    if(existing>=0){ selected_=existing; ListView_SetItemState(list_,existing,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED); RefreshSelected(); return; }
    settings_.applications.push_back({name.empty()?Stem(path):name,exe,path,70}); selected_=static_cast<int>(settings_.applications.size())-1; Save(); RefreshList();
    ListView_SetItemState(list_,selected_,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED); RefreshSelected(); ApplyForForeground();
}

void AppWindow::AddRunning(){
    auto apps=EnumerateRunningApplications(); if(apps.empty()){ SetStatus(L"No selectable running applications found."); return; }
    HMENU menu=CreatePopupMenu(); for(UINT i=0;i<apps.size()&&i<3000;++i){ std::wstring label=apps[i].name+L"  ["+apps[i].executable+L"]"; AppendMenuW(menu,MF_STRING,10000+i,label.c_str()); }
    RECT button{}; GetWindowRect(GetDlgItem(window_,ID_ADD),&button); UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,button.left,button.bottom,0,window_,nullptr); DestroyMenu(menu);
    if(choice>=10000&&choice<10000+apps.size()){ auto& app=apps[choice-10000]; AddProfile(app.name,app.path); }
}
void AppWindow::AddManual(){
    wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner=window_; dialog.lpstrFilter=L"Applications (*.exe)\0*.exe\0All files\0*.*\0"; dialog.lpstrFile=path; dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_DONTADDTORECENT; if(GetOpenFileNameW(&dialog)) AddProfile(Stem(path),path);
}
void AppWindow::RemoveSelected(){ if(selected_<0||selected_>=static_cast<int>(settings_.applications.size()))return; settings_.applications.erase(settings_.applications.begin()+selected_); selected_=-1; Save(); RefreshList(); RefreshSelected(); ApplyForForeground(); }

void AppWindow::ReinitializeGpu(){
    Trace(L"GPU begin");
    if(gpu_) gpu_->RestoreOriginal(); std::wstring display,diagnostic; gpu_=CreateBestBackend(display,diagnostic); SetStatus(diagnostic); ApplyForForeground();
    Trace(L"GPU end");
}
void AppWindow::EmergencyRestore() noexcept { if(gpu_) gpu_->RestoreOriginal(); }

LRESULT AppWindow::HandleMessage(UINT message, WPARAM wp, LPARAM lp) {
    switch(message){
    case WM_CREATE: CreateControls(); Layout(); return 0;
    case WM_PAINT:Paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:DrawOwnerItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:{
        HDC dc=reinterpret_cast<HDC>(wp);HWND control=reinterpret_cast<HWND>(lp);SetBkMode(dc,TRANSPARENT);
        const bool hint=control==subtitleLabel_||control==defaultHint_||control==applicationsHint_||control==selectedHint_;
        SetTextColor(dc,hint?(dark_?RGB(197,197,197):RGB(93,93,93)):(dark_?RGB(255,255,255):RGB(27,27,27)));
        if(control==status_) return reinterpret_cast<LRESULT>(pillBrush_);
        const bool onProfileCard=control==selectedLabel_||control==selectedHint_||control==appSlider_;
        if(onProfileCard) return reinterpret_cast<LRESULT>(selectedBrush_);
        const bool onCard=control==defaultLabel_||control==defaultHint_||control==applicationsLabel_||control==applicationsHint_||control==defaultSlider_;
        return reinterpret_cast<LRESULT>(onCard?cardBrush_:backgroundBrush_);
    }
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={Scale(660,dpi_),Scale(560,dpi_)};return 0;}
    case WM_DPICHANGED: dpi_=HIWORD(wp);SetWindowPos(window_,nullptr,reinterpret_cast<RECT*>(lp)->left,reinterpret_cast<RECT*>(lp)->top,
        reinterpret_cast<RECT*>(lp)->right-reinterpret_cast<RECT*>(lp)->left,reinterpret_cast<RECT*>(lp)->bottom-reinterpret_cast<RECT*>(lp)->top,SWP_NOZORDER|SWP_NOACTIVATE);RecreateFonts();UpdateIcons();RefreshList();Layout();return 0;
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(lp)==defaultSlider_){ settings_.defaultVibrance=static_cast<int>(SendMessageW(defaultSlider_,TBM_GETPOS,0,0)); SetWindowTextW(defaultValue_,std::to_wstring(settings_.defaultVibrance).c_str()); RedrawWindow(defaultSlider_,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);Save(); ApplyForForeground(); }
        else if(reinterpret_cast<HWND>(lp)==appSlider_&&selected_>=0){ settings_.applications[selected_].vibrance=static_cast<int>(SendMessageW(appSlider_,TBM_GETPOS,0,0)); SetWindowTextW(appValue_,std::to_wstring(settings_.applications[selected_].vibrance).c_str()); RedrawWindow(appSlider_,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);RedrawWindow(list_,nullptr,nullptr,RDW_INVALIDATE); Save(); ApplyForForeground(); }
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case ID_ADD:AddRunning();break; case ID_ADD_MANUAL:AddManual();break; case ID_REMOVE:RemoveSelected();break;
        case ID_STARTUP:{ bool enabled=!startupChecked_; std::wstring error; if(SetStartWithWindows(enabled,ExePath(),error)){startupChecked_=enabled;settings_.startWithWindows=enabled;Save();}else{SetStatus(error);} InvalidateRect(startup_,nullptr,TRUE); break;}
        case ID_EXIT:exiting_=true;DestroyWindow(window_);break;
        } return 0;
    case WM_NOTIFY:{auto* header=reinterpret_cast<NMHDR*>(lp);if((header->idFrom==ID_DEFAULT||header->idFrom==ID_APP_SLIDER)&&header->code==NM_CUSTOMDRAW)return DrawSlider(reinterpret_cast<NMCUSTOMDRAW*>(lp));
        if(header->idFrom==ID_LIST&&header->code==LVN_ITEMCHANGED){auto* n=reinterpret_cast<NMLISTVIEW*>(lp);if((n->uNewState&LVIS_SELECTED)&&n->iItem>=0){selected_=n->iItem;RefreshSelected();}}return 0;}
    case WM_CLOSE: ShowWindow(window_,SW_HIDE); return 0;
    case ShowExistingMessage: ShowWindow(window_,SW_RESTORE); SetForegroundWindow(window_); FlashWindow(window_,TRUE); return 0;
    case WM_FOREGROUND_CHANGED: ApplyForForeground(); return 0;
    case WM_ACCENT_CHANGED: UpdateAccent(); InvalidateRect(window_,nullptr,TRUE); return 0;
    case WM_DISPLAYCHANGE: case WM_DEVICECHANGE: KillTimer(window_,TIMER_GPU_REINIT); SetTimer(window_,TIMER_GPU_REINIT,1200,nullptr); return 0;
    case WM_POWERBROADCAST: if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND) SetTimer(window_,TIMER_GPU_REINIT,1200,nullptr); return TRUE;
    case WM_TIMER: if(wp==TIMER_GPU_REINIT){KillTimer(window_,TIMER_GPU_REINIT);ReinitializeGpu();} return 0;
    case WM_QUERYENDSESSION: EmergencyRestore(); return TRUE;
    case WM_ENDSESSION: if(wp) EmergencyRestore(); return 0;
    case WM_DESTROY: monitor_.Stop(); EmergencyRestore(); Save(); ReleaseIcons(); if(font_)DeleteObject(font_);if(titleFont_)DeleteObject(titleFont_);if(sectionFont_)DeleteObject(sectionFont_);if(smallFont_)DeleteObject(smallFont_);if(microFont_)DeleteObject(microFont_);if(monoFont_)DeleteObject(monoFont_);if(backgroundBrush_)DeleteObject(backgroundBrush_);if(cardBrush_)DeleteObject(cardBrush_);if(selectedBrush_)DeleteObject(selectedBrush_);if(pillBrush_)DeleteObject(pillBrush_); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window_,message,wp,lp);
}

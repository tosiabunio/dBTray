#include "App.h"

#include "AppMessages.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <cwchar>

namespace {

constexpr wchar_t kTip[] = L"dBTray output level";
// Windows shows no indicator while an app listens to the output, so the icon says it.
constexpr wchar_t kMeasuringTip[] = L"dBTray output level\nMeasuring loudness while closed";

constexpr UINT kOpenMeterCommand = 1;
constexpr UINT kQuitCommand = 2;

// Tells the app window when the default output device changes, so capture can move to it.
class DeviceNotifier final : public IMMNotificationClient {
public:
    explicit DeviceNotifier(HWND window) : window_(window) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG references = InterlockedDecrement(&references_);
        if (references == 0) delete this;
        return references;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // Called on a system thread.
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) PostMessageW(window_, WM_APP_DEVICE_CHANGED, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    ~DeviceNotifier() = default;

    LONG references_ = 1;
    HWND window_;
};

// Makes the tray icon's context menu follow the theme. Windows has no public switch for dark
// menus; these uxtheme exports (by ordinal, stable since Windows 10 1903) are what apps use.
void ApplyMenuTheme(bool dark) {
    static const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    using FlushMenuThemes = void(WINAPI*)();
    const auto setPreferredAppMode =
        reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    const auto flushMenuThemes =
        reinterpret_cast<FlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setPreferredAppMode) setPreferredAppMode(dark ? 2 /* ForceDark */ : 3 /* ForceLight */);
    if (flushMenuThemes) flushMenuThemes();
}

}  // namespace

int App::Run(HINSTANCE instance, bool openMeter) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    settings_.Load();
    palette_ = MakePalette();
    ApplyMenuTheme(palette_.dark);
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    // A hidden top-level window rather than a message-only one, so broadcasts (theme changes,
    // TaskbarCreated) reach it.
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    windowClass.lpszClassName = kAppWindowClass;
    RegisterClassExW(&windowClass);
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, L"dBTray", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, this);
    if (!window_ || !meter_.Create(instance, window_)) return 1;

    model_.Attach(window_);
    // If the taskbar isn't up yet (at sign-in), TaskbarCreated adds the icon later.
    tray_.Add(window_, WM_APP_TRAY);
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&deviceEnumerator_)))) {
        deviceNotifier_ = new DeviceNotifier(window_);
        deviceEnumerator_->RegisterEndpointNotificationCallback(deviceNotifier_);
    }
    if (openMeter) PostMessageW(window_, WM_APP_SHOW_METER, 0, 0);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (deviceNotifier_) {
        deviceEnumerator_->UnregisterEndpointNotificationCallback(deviceNotifier_);
        deviceNotifier_->Release();
        deviceNotifier_ = nullptr;
    }
    deviceEnumerator_.Reset();
    model_.Stop();
    if (SUCCEEDED(com)) CoUninitialize();
    return int(message.wParam);
}

void App::Quit() {
    PostMessageW(window_, WM_CLOSE, 0, 0);
}

void App::ShowMeter() {
    launchAtLogin_.Refresh();
    model_.SetVisible(true);
    model_.Start();
    RECT icon;
    meter_.Show(tray_.GetRect(icon) ? &icon : nullptr);
    shownAt_ = GetTickCount64();
    UpdateTrayTip();
}

void App::OnMeterHidden(bool deactivated) {
    if (deactivated) {
        POINT cursor;
        RECT icon;
        if (GetCursorPos(&cursor) && tray_.GetRect(icon) && PtInRect(&icon, cursor)) {
            ignoreSelectUntil_ = GetTickCount64() + 1000;
        }
    }
    if (settings_.KeepsMeasuringWhenClosed()) {
        model_.SetVisible(false);
    } else {
        model_.Stop();
    }
    UpdateTrayTip();
}

void App::UpdateTrayTip() {
    tray_.SetTip(!meter_.IsVisible() && model_.IsCapturing() ? kMeasuringTip : kTip);
}

void App::RefreshTheme() {
    palette_ = MakePalette();
    ApplyMenuTheme(palette_.dark);
    tray_.UpdateImage();
    meter_.OnThemeChanged();
}

void App::OnTrayEvent(UINT event, POINT position) {
    switch (event) {
    case NIN_SELECT:
    case NIN_KEYSELECT: {
        const ULONGLONG now = GetTickCount64();
        if (now < ignoreSelectUntil_) {
            ignoreSelectUntil_ = 0;
        } else if (!meter_.IsVisible()) {
            ShowMeter();
        } else if (now - shownAt_ > 300) {  // Enter reports NIN_KEYSELECT twice
            meter_.Hide();
        }
        break;
    }
    case WM_CONTEXTMENU:
        ShowMenu(position);
        break;
    }
}

void App::ShowMenu(POINT position) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kOpenMeterCommand, L"Open Meter");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuitCommand, L"Quit dBTray");
    SetMenuDefaultItem(menu, kOpenMeterCommand, FALSE);

    // The menu only closes on a click elsewhere if its owner is the foreground window.
    SetForegroundWindow(window_);
    const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT command = UINT(TrackPopupMenuEx(menu, alignment | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                               position.x, position.y, window_, nullptr));
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kOpenMeterCommand && !meter_.IsVisible()) ShowMeter();
    if (command == kQuitCommand) Quit();
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self;
    if (message == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // Explorer restarted: its new taskbar has none of the old icons.
    if (taskbarCreatedMessage_ && message == taskbarCreatedMessage_) {
        tray_.Add(window_, WM_APP_TRAY);
        UpdateTrayTip();
        return 0;
    }

    switch (message) {
    case WM_APP_TRAY:
        OnTrayEvent(LOWORD(lParam), {GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
        return 0;
    case WM_APP_SHOW_METER:
        if (meter_.IsVisible()) {
            SetForegroundWindow(meter_.Handle());
        } else {
            ShowMeter();
        }
        return 0;
    case WM_APP_DEVICE_CHANGED:
        model_.OnDefaultDeviceChanged();
        meter_.Update();
        UpdateTrayTip();
        return 0;
    case WM_APP_CAPTURE_FAILED:
        model_.OnCaptureFailed(lParam);
        return 0;
    case WM_TIMER:
        if (wParam == kTickTimer) {
            if (model_.Tick() && meter_.IsVisible()) meter_.Invalidate();
        } else if (wParam == kRestartTimer) {
            model_.Restart();
            meter_.Update();
            UpdateTrayTip();
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            RefreshTheme();
        } else {
            tray_.UpdateImage();
        }
        return 0;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        RefreshTheme();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        tray_.UpdateImage();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        model_.Stop();
        tray_.Remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

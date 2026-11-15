#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "LaunchAtLogin.h"
#include "LevelMeterModel.h"
#include "MeterWindow.h"
#include "Settings.h"
#include "Theme.h"
#include "TrayIcon.h"

// Owns the tray icon and the meter flyout. Audio is only captured while the flyout is open, so
// the app costs nothing the rest of the time, unless the user chose to keep measuring loudness
// while it's closed.
class App {
public:
    App() : meter_(*this) {}
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance, bool openMeter);
    void Quit();

    LevelMeterModel& Model() { return model_; }
    Settings& Preferences() { return settings_; }
    LaunchAtLogin& Login() { return launchAtLogin_; }
    const Palette& Colors() const { return palette_; }

    // Called by the flyout after it closed; `deactivated` when a click elsewhere closed it.
    void OnMeterHidden(bool deactivated);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnTrayEvent(UINT event, POINT position);
    void ShowMeter();
    void ShowMenu(POINT position);
    void RefreshTheme();
    void UpdateTrayTip();

    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    Settings settings_;
    LaunchAtLogin launchAtLogin_;
    Palette palette_;
    LevelMeterModel model_;
    TrayIcon tray_;
    MeterWindow meter_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    IMMNotificationClient* deviceNotifier_ = nullptr;
    // Clicking the tray icon while the flyout is open closes it (the click takes the focus away)
    // and then reports the click; these keep that click from opening it again.
    ULONGLONG ignoreSelectUntil_ = 0;
    ULONGLONG shownAt_ = 0;
};

#pragma once

#include <windows.h>

// Private messages and timers handled by the app's hidden window.
constexpr UINT WM_APP_TRAY = WM_APP + 1;            // tray icon callback
constexpr UINT WM_APP_DEVICE_CHANGED = WM_APP + 2;  // the default output device changed
constexpr UINT WM_APP_CAPTURE_FAILED = WM_APP + 3;  // wParam: HRESULT, lParam: capture generation
constexpr UINT WM_APP_SHOW_METER = WM_APP + 4;      // posted by a second instance

constexpr UINT_PTR kTickTimer = 1;     // meter refresh
constexpr UINT_PTR kRestartTimer = 2;  // capture restart after a failure

constexpr wchar_t kAppWindowClass[] = L"dBTray.App";

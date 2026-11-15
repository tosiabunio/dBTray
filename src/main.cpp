#include <windows.h>

#include <cwchar>

#include "App.h"
#include "AppMessages.h"
#include "LaunchAtLogin.h"

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR commandLine, _In_ int) {
    // Started by hand, the app opens the meter so there's something to see: Windows 11 tucks new
    // tray icons away in the overflow menu. At sign-in it starts quietly.
    const bool atStartup = wcsstr(commandLine, LaunchAtLogin::kStartupSwitch) != nullptr;

    // One meter per session is enough. A second launch opens the first one's flyout instead.
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\dBTray.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND running = FindWindowW(kAppWindowClass, nullptr); running && !atStartup) {
            DWORD process = 0;
            GetWindowThreadProcessId(running, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(running, WM_APP_SHOW_METER, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    App app;
    const int exitCode = app.Run(instance, !atStartup);
    if (mutex) CloseHandle(mutex);
    return exitCode;
}

#pragma once

#include <string>

// Starts the app at sign-in through the per-user Run key. The registry (and Settings › Apps ›
// Startup, where the user can switch the entry off) is the source of truth, so the status is
// re-read rather than stored.
class LaunchAtLogin {
public:
    // Command-line switch the startup entry passes, so the meter doesn't pop open at sign-in.
    static constexpr wchar_t kStartupSwitch[] = L"--startup";

    void Refresh();

    // The startup entry points at this copy of the app.
    bool IsEnabled() const { return registeredHere_; }
    void SetEnabled(bool enabled);

    // Registered, but switched off under Settings › Apps › Startup.
    bool DisabledInSettings() const { return registeredHere_ && disabledInSettings_; }
    // The startup entry points at another copy of the app (this one was moved or copied).
    bool RegisteredElsewhere() const { return registeredElsewhere_; }
    const std::wstring& ErrorMessage() const { return errorMessage_; }

    static void OpenSettings();

private:
    bool registeredHere_ = false;
    bool registeredElsewhere_ = false;
    bool disabledInSettings_ = false;
    std::wstring errorMessage_;
};

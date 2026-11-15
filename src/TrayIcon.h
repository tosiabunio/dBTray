#pragma once

#include <windows.h>

#include <string>

// The notification-area icon: two LED-style level bars (L over R) echoing the meter in the
// flyout. Windows has no template images, so the glyph is drawn white or black to suit the
// taskbar's theme, at the taskbar's icon size; unlit segments are translucent.
class TrayIcon {
public:
    ~TrayIcon();

    bool Add(HWND window, UINT callbackMessage);
    void Remove();
    // Redraws the glyph if the taskbar's theme or icon size changed.
    void UpdateImage();
    void SetTip(const wchar_t* tip);
    // Screen rectangle of the icon, if it's on the taskbar.
    bool GetRect(RECT& rect) const;

private:
    HWND window_ = nullptr;
    UINT callbackMessage_ = 0;
    HICON icon_ = nullptr;
    int iconSize_ = 0;
    bool iconForLightTaskbar_ = false;
    bool added_ = false;
    std::wstring tip_ = L"dBTray output level";
};

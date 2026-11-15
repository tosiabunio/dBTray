#include "TrayIcon.h"

#include "Theme.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr UINT kIconId = 1;
constexpr BYTE kUnlitAlpha = 77;  // 0.3

// Whole-pixel geometry of the two bars for an icon of `size` pixels, scaled from dBar's 18 pt
// menu bar glyph: six 2 pt segments 1 pt apart, in two 5 pt bars 2 pt apart.
struct Geometry {
    int segments, width, gap, height, rowGap;
};

Geometry GeometryFor(int size) {
    if (size <= 16) return {5, 2, 1, 5, 2};  // six segments don't fit in 16 px
    const double scale = size / 18.0;
    Geometry g{6, std::max(1, int(std::lround(2 * scale))), std::max(1, int(std::lround(scale))),
               std::max(2, int(std::lround(5 * scale))), std::max(1, int(std::lround(2 * scale)))};
    while (g.width > 1 && g.segments * g.width + (g.segments - 1) * g.gap > size) --g.width;
    return g;
}

int TaskbarIconSize() {
    UINT dpi = 0;
    if (const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) dpi = GetDpiForWindow(taskbar);
    if (!dpi) dpi = GetDpiForSystem();
    return GetSystemMetricsForDpi(SM_CXSMICON, dpi);
}

HICON CreateGlyph(int size, bool lightTaskbar) {
    const Geometry g = GeometryFor(size);
    // Lit segments in the top (L) and bottom (R) bars: 4 and 3 of 6, as in dBar.
    const int lit[2] = {g.segments - 2, g.segments - 3};
    const int left = (size - (g.segments * g.width + (g.segments - 1) * g.gap)) / 2;
    const int top = (size - (2 * g.height + g.rowGap)) / 2;
    const uint32_t color = lightTaskbar ? 0x000000 : 0xFFFFFF;

    std::vector<uint32_t> pixels(size_t(size) * size, 0);  // BGRA, straight alpha
    for (int row = 0; row < 2; ++row) {
        for (int segment = 0; segment < g.segments; ++segment) {
            const uint32_t pixel = uint32_t(segment < lit[row] ? 255 : kUnlitAlpha) << 24 | color;
            const int x0 = left + segment * (g.width + g.gap);
            const int y0 = top + row * (g.height + g.rowGap);
            for (int y = std::max(0, y0); y < std::min(size, y0 + g.height); ++y) {
                for (int x = std::max(0, x0); x < std::min(size, x0 + g.width); ++x) {
                    pixels[size_t(y) * size + x] = pixel;
                }
            }
        }
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP colorBitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!colorBitmap) return nullptr;
    std::memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
    // The alpha channel does the masking; the mask bitmap only has to exist.
    const std::vector<BYTE> zeros(size_t((size + 15) / 16) * 2 * size, 0);
    const HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, zeros.data());

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    const HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

NOTIFYICONDATAW IconData(HWND window, UINT flags) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kIconId;
    data.uFlags = flags;
    return data;
}

}  // namespace

TrayIcon::~TrayIcon() {
    Remove();
    if (icon_) DestroyIcon(icon_);
}

bool TrayIcon::Add(HWND window, UINT callbackMessage) {
    window_ = window;
    callbackMessage_ = callbackMessage;
    UpdateImage();

    // The icon is identified by window and id rather than by GUID: a GUID is tied to the
    // executable's path, which a portable app can't count on.
    NOTIFYICONDATAW data = IconData(window_, NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP);
    data.uCallbackMessage = callbackMessage_;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tip_.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (added_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    return added_;
}

void TrayIcon::Remove() {
    if (!added_) return;
    NOTIFYICONDATAW data = IconData(window_, 0);
    Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
}

void TrayIcon::UpdateImage() {
    const int size = TaskbarIconSize();
    const bool light = SystemUsesLightTheme();
    if (icon_ && size == iconSize_ && light == iconForLightTaskbar_) return;
    const HICON icon = CreateGlyph(size, light);
    if (!icon) return;
    const HICON previous = icon_;
    icon_ = icon;
    iconSize_ = size;
    iconForLightTaskbar_ = light;
    if (added_) {
        NOTIFYICONDATAW data = IconData(window_, NIF_ICON);
        data.hIcon = icon_;
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    if (previous) DestroyIcon(previous);
}

void TrayIcon::SetTip(const wchar_t* tip) {
    if (tip_ == tip) return;
    tip_ = tip;
    if (!added_) return;
    NOTIFYICONDATAW data = IconData(window_, NIF_TIP | NIF_SHOWTIP);
    wcsncpy_s(data.szTip, tip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

bool TrayIcon::GetRect(RECT& rect) const {
    if (!added_) return false;
    NOTIFYICONIDENTIFIER identifier = {};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = window_;
    identifier.uID = kIconId;
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect));
}

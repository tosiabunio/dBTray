#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "LevelMeterModel.h"

class App;

// The meter flyout that opens from the tray icon: L/R peak meters, the optional loudness bars and
// the options. It is drawn with Direct2D so it can follow the Windows 11 look in both themes, and
// closes like a system flyout when it loses focus.
class MeterWindow {
public:
    explicit MeterWindow(App& app);
    MeterWindow(const MeterWindow&) = delete;
    MeterWindow& operator=(const MeterWindow&) = delete;

    bool Create(HINSTANCE instance, HWND owner);
    // Opens next to the tray icon; `icon` is its screen rectangle, or null if that's unknown.
    void Show(const RECT* icon);
    void Hide() { HideWindow(false); }
    bool IsVisible() const;
    HWND Handle() const { return hwnd_; }
    void Invalidate();
    // Lays the window out again after something that changes its contents (options, errors).
    void Update();
    void OnThemeChanged();

    struct TextStyle {
        float size;  // DIPs
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
        bool wraps = false;
        bool tabularFigures = false;
        bool icon = false;  // drawn with the system icon font
    };

private:
    enum class Control { None, Meters, KeepMeasuring, Loudness, LaunchAtLogin, Quit, OpenSettings };

    struct CachedFormat {
        float size;
        DWRITE_FONT_WEIGHT weight;
        bool icon;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    };

    // Everything is in DIPs.
    struct Layout {
        float height = 0;
        bool showsLoudness = false;
        std::wstring deviceName;  // truncated to fit the header
        float titleWidth = 0;
        std::wstring error;
        D2D1_RECT_F header{}, errorRect{};
        D2D1_RECT_F shortTermRow{}, leftRow{}, rightRow{}, integratedRow{}, scaleRow{};
        D2D1_RECT_F caption{}, keepMeasuring{};
        float dividerY = 0;
        D2D1_RECT_F loudnessToggle{}, launchToggle{}, quitButton{};
        std::wstring note;
        D2D1_RECT_F noteRect{};
        bool hasLink = false;
        D2D1_RECT_F linkRect{};
        std::wstring launchError;
        D2D1_RECT_F launchErrorRect{};
    };

    struct Columns {
        float label, barLeft, barRight, clipLeft, readoutLeft, right;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void HideWindow(bool deactivated);

    // Layout and placement.
    void Relayout();
    void Place();
    void Resize();
    void SetDpi(UINT dpi);
    void RebuildTooltips();
    float Scale() const { return float(dpi_) / 96.0f; }
    int Pixels(float dips) const;
    float Snap(float dips) const;
    RECT ToPixels(const D2D1_RECT_F& rect) const;
    static Columns ColumnsFor(const D2D1_RECT_F& row);
    D2D1_RECT_F CheckboxRect(float x, float y, float height, const wchar_t* label);

    // Input.
    Control HitTest(float x, float y) const;
    D2D1_RECT_F ControlRect(Control control) const;
    std::vector<Control> FocusOrder() const;
    void MoveFocus(int step);
    void Activate(Control control);

    // Drawing.
    bool EnsureTarget();
    void ReleaseGraphics();
    void Render();
    void DrawHeader();
    void DrawWarning(const D2D1_RECT_F& rect, const std::wstring& text);
    void DrawChannelRow(const D2D1_RECT_F& row, const wchar_t* name, const LevelMeterModel::Channel& channel);
    void DrawLoudnessRow(const D2D1_RECT_F& row, const wchar_t* label, float value);
    void DrawSegmentedBar(const D2D1_RECT_F& rect, float level, float peakHold);
    void DrawLoudnessBar(const D2D1_RECT_F& rect, float value);
    void DrawScale(const D2D1_RECT_F& row);
    void DrawCheckbox(const D2D1_RECT_F& rect, const wchar_t* label, bool checked, Control control);
    void DrawButton(const D2D1_RECT_F& rect, const wchar_t* label, Control control);
    void DrawFocusRing();
    void FillRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color);
    void FillRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color);
    void StrokeRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color, float width);

    // Text.
    IDWriteTextFormat* Format(const TextStyle& style);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> MakeLayout(const std::wstring& text, const TextStyle& style,
                                                         float width, float height);
    D2D1_SIZE_F Measure(const std::wstring& text, const TextStyle& style, float maxWidth = 10000.0f);
    void DrawString(const std::wstring& text, const D2D1_RECT_F& rect, const TextStyle& style,
                    const D2D1_COLOR_F& color);
    std::wstring TruncateMiddle(const std::wstring& text, const TextStyle& style, float maxWidth);

    App& app_;
    HWND hwnd_ = nullptr;
    HWND tooltip_ = nullptr;
    UINT dpi_ = 96;
    UINT edge_ = 0;  // taskbar edge the flyout sits against (ABE_*)
    RECT iconRect_{};
    bool hasIcon_ = false;
    bool positioning_ = false;
    bool hiding_ = false;

    Layout layout_;
    Control hover_ = Control::None;
    Control pressed_ = Control::None;
    Control focus_ = Control::None;
    bool showsFocus_ = false;  // only after keyboard navigation, as Windows does
    bool trackingMouse_ = false;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTypography> tabularFigures_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<CachedFormat> formats_;
    std::wstring textFamily_, smallTextFamily_, iconFamily_;
    wchar_t locale_[LOCALE_NAME_MAX_LENGTH] = L"";

    std::wstring loudnessCaption_, shortTermHelp_, integratedHelp_;
};

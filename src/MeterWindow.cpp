#include "MeterWindow.h"

#include "App.h"
#include "Loudness.h"
#include "MeterScale.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <initializer_list>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"dBTray.Meter";

// Layout in DIPs. The column widths are shared by the meter rows and the scale so the ticks line
// up with the bars.
constexpr float kWidth = 380;
constexpr float kPadding = 16;
constexpr float kSectionSpacing = 12;
constexpr float kRowSpacing = 6;
constexpr float kColumnSpacing = 8;
constexpr float kLabelWidth = 12;
constexpr float kClipIndicatorSize = 8;
constexpr float kReadoutWidth = 44;
constexpr float kBarHeight = 12;
constexpr float kLoudnessBarHeight = 5;
constexpr float kLoudnessRowHeight = 9;
constexpr float kScaleHeight = 12;
constexpr float kHeaderHeight = 20;
constexpr float kCheckboxSize = 18;
constexpr float kCheckboxLabelGap = 8;
constexpr float kCheckboxRowHeight = 22;
constexpr float kControlHeight = 28;
constexpr float kControlRadius = 4;
constexpr float kCloseButtonSize = 28;
constexpr float kCloseButtonOverhang = 8;  // into the right padding, so the glyph lines up with the content
constexpr float kNoteHeight = 18;
constexpr float kWarningIndent = 20;
constexpr float kScreenMargin = 12;
constexpr float kSegmentWidth = 3;
constexpr float kSegmentGap = 1;
constexpr float kUnlitOpacity = 0.18f;

using Style = MeterWindow::TextStyle;
constexpr auto kRegular = DWRITE_FONT_WEIGHT_NORMAL;
constexpr auto kSemibold = DWRITE_FONT_WEIGHT_SEMI_BOLD;
constexpr auto kLeading = DWRITE_TEXT_ALIGNMENT_LEADING;
constexpr auto kCenter = DWRITE_TEXT_ALIGNMENT_CENTER;
constexpr auto kTrailing = DWRITE_TEXT_ALIGNMENT_TRAILING;

const Style kTitleStyle{14, kSemibold};
const Style kDeviceStyle{12, kRegular, kTrailing};
const Style kChannelLabelStyle{12, kSemibold, kCenter};
const Style kChannelReadoutStyle{12, kRegular, kTrailing, false, true};
const Style kLoudnessLabelStyle{11, kSemibold, kCenter};
const Style kLoudnessReadoutStyle{11, kRegular, kTrailing, false, true};
const Style kTickStyle{10, kRegular, kLeading, false, true};
const Style kUnitStyle{10, kRegular, kTrailing};
const Style kCaptionStyle{11, kRegular, kLeading, true};
const Style kControlStyle{13};
const Style kButtonStyle{13, kRegular, kCenter};
const Style kNoteStyle{12};
const Style kWarningStyle{12, kRegular, kLeading, true};
const Style kHeaderIconStyle{16, kRegular, kLeading, false, false, true};
const Style kWarningIconStyle{13, kRegular, kLeading, false, false, true};
const Style kCheckIconStyle{12, kRegular, kCenter, false, false, true};
const Style kCloseIconStyle{10, kRegular, kCenter, false, false, true};

// Segoe Fluent Icons (Segoe MDL2 Assets on Windows 10) code points.
constexpr wchar_t kSpeakerGlyph[] = L"\xE994";
constexpr wchar_t kWarningGlyph[] = L"\xE7BA";
constexpr wchar_t kCheckGlyph[] = L"\xE73E";
constexpr wchar_t kCloseGlyph[] = L"\xE8BB";
constexpr wchar_t kInfinity[] = L"-\x221E";
constexpr wchar_t kEllipsis[] = L"\x2026";

constexpr wchar_t kTitle[] = L"Output Level";
constexpr wchar_t kKeepMeasuringLabel[] = L"Keep measuring when closed";
constexpr wchar_t kLoudnessLabel[] = L"Loudness";
constexpr wchar_t kLaunchAtLoginLabel[] = L"Launch at Login";
constexpr wchar_t kQuitLabel[] = L"Quit dBTray";
constexpr wchar_t kOpenSettingsLabel[] = L"Open Settings";
constexpr wchar_t kDisabledNote[] = L"Turn dBTray on in Startup apps to finish.";
constexpr wchar_t kOtherCopyNote[] = L"Launch at Login is set for another copy of dBTray.";

constexpr wchar_t kCloseHelp[] = L"Close";
constexpr wchar_t kPeakHelp[] = L"Peak since last reset. Click to reset peaks and integrated loudness.";
constexpr wchar_t kClipHelp[] = L"Clip indicator: lights when the signal reaches 0 dBFS";
constexpr wchar_t kLoudnessHelp[] = L"Show short-term and integrated loudness (LUFS)";
constexpr wchar_t kKeepMeasuringHelp[] =
    L"Integrated loudness keeps adding up while the meter is closed. The dBTray icon's tooltip "
    L"says so while it measures.";

std::wstring FormatReadout(float value) {
    if (!(value > MeterScale::kFloor)) return kInfinity;
    wchar_t text[16];
    swprintf_s(text, L"%.1f", value);
    return text;
}

std::wstring PickFamily(IDWriteFactory* factory, std::initializer_list<const wchar_t*> names) {
    ComPtr<IDWriteFontCollection> fonts;
    factory->GetSystemFontCollection(&fonts);
    for (const wchar_t* name : names) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) return name;
    }
    return *(names.end() - 1);
}

const D2D1_COLOR_F& ZoneColor(const Palette& palette, float position) {
    switch (MeterScale::ZoneAt(position)) {
    case MeterScale::Zone::Red: return palette.red;
    case MeterScale::Zone::Yellow: return palette.yellow;
    default: return palette.green;
    }
}

bool Contains(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// Meter rows are thin; the gaps between them count as part of the rows for the mouse.
D2D1_RECT_F RowTarget(const D2D1_RECT_F& row) {
    return {row.left, row.top - kRowSpacing / 2, row.right, row.bottom + kRowSpacing / 2};
}

D2D1_RECT_F Centered(float left, float right, float centerY, float height = 20) {
    return {left, centerY - height / 2, right, centerY + height / 2};
}

float CenterY(const D2D1_RECT_F& rect) {
    return (rect.top + rect.bottom) / 2;
}

}  // namespace

MeterWindow::MeterWindow(App& app) : app_(app) {
    const std::wstring target = std::to_wstring(int(Loudness::kTarget));
    const std::wstring tail =
        L" in LUFS. Target " + target + L" LUFS. Click to reset peaks and integrated loudness.";
    loudnessCaption_ = L"LUFS: S short-term (3 s), I integrated. Target " + target + L".";
    shortTermHelp_ = L"Short-term loudness (last 3 seconds)" + tail;
    integratedHelp_ = L"Integrated loudness (since measuring started or was reset)" + tail;
}

bool MeterWindow::Create(HINSTANCE instance, HWND owner) {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        return false;
    }
    if (SUCCEEDED(dwrite_->CreateTypography(&tabularFigures_))) {
        tabularFigures_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    }
    GetUserDefaultLocaleName(locale_, LOCALE_NAME_MAX_LENGTH);
    textFamily_ = PickFamily(dwrite_.Get(), {L"Segoe UI Variable Text", L"Segoe UI"});
    smallTextFamily_ = PickFamily(dwrite_.Get(), {L"Segoe UI Variable Small", textFamily_.c_str()});
    iconFamily_ = PickFamily(dwrite_.Get(), {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"dBTray Output Level",
                            WS_POPUP, 0, 0, 0, 0, owner, nullptr, instance, this);
    if (!hwnd_) return false;

    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, instance, nullptr);
    OnThemeChanged();
    return true;
}

bool MeterWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void MeterWindow::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MeterWindow::OnThemeChanged() {
    if (!hwnd_) return;
    const BOOL dark = app_.Colors().dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (tooltip_) SetWindowTheme(tooltip_, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    Invalidate();
}

void MeterWindow::Show(const RECT* icon) {
    hasIcon_ = icon != nullptr;
    if (icon) iconRect_ = *icon;
    hover_ = pressed_ = focus_ = Control::None;
    showsFocus_ = false;
    Relayout();
    Place();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateWindow(hwnd_);
}

void MeterWindow::Hide() {
    if (!IsVisible()) return;
    ShowWindow(hwnd_, SW_HIDE);
    hover_ = pressed_ = focus_ = Control::None;
    ReleaseGraphics();
    app_.OnMeterHidden();
    // Hand the pages the flyout touched back to the system; a tray app should sit small.
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
}

void MeterWindow::Reposition(const RECT* icon) {
    if (!IsVisible()) return;
    hasIcon_ = icon != nullptr;
    if (icon) iconRect_ = *icon;
    Place();
}

void MeterWindow::Update() {
    if (!hwnd_) return;
    Relayout();
    if (IsVisible()) Resize();
    Invalidate();
}

// MARK: Layout

int MeterWindow::Pixels(float dips) const {
    return int(std::ceil(dips * Scale() - 0.01f));
}

float MeterWindow::Snap(float dips) const {
    return std::round(dips * Scale()) / Scale();
}

RECT MeterWindow::ToPixels(const D2D1_RECT_F& rect) const {
    const float scale = Scale();
    return {LONG(std::floor(rect.left * scale)), LONG(std::floor(rect.top * scale)),
            LONG(std::ceil(rect.right * scale)), LONG(std::ceil(rect.bottom * scale))};
}

MeterWindow::Columns MeterWindow::ColumnsFor(const D2D1_RECT_F& row) {
    Columns columns;
    columns.label = row.left;
    columns.barLeft = row.left + kLabelWidth + kColumnSpacing;
    columns.right = row.right;
    columns.readoutLeft = row.right - kReadoutWidth;
    columns.clipLeft = columns.readoutLeft - kColumnSpacing - kClipIndicatorSize;
    columns.barRight = columns.clipLeft - kColumnSpacing;
    return columns;
}

D2D1_RECT_F MeterWindow::CheckboxRect(float x, float y, float height, const wchar_t* label) {
    const float width = kCheckboxSize + kCheckboxLabelGap + std::ceil(Measure(label, kControlStyle).width);
    return {x, y, x + width, y + height};
}

void MeterWindow::Relayout() {
    const LevelMeterModel& model = app_.Model();
    const LaunchAtLogin& login = app_.Login();
    const float left = kPadding;
    const float right = kWidth - kPadding;

    Layout layout;
    layout.showsLoudness = app_.Preferences().ShowsLoudness();
    layout.error = model.ErrorMessage();
    float y = kPadding;

    layout.header = {left, y, right, y + kHeaderHeight};
    const float closeRight = right + kCloseButtonOverhang;
    layout.closeButton = Centered(closeRight - kCloseButtonSize, closeRight, CenterY(layout.header), kCloseButtonSize);
    layout.titleWidth = std::ceil(Measure(kTitle, kTitleStyle).width);
    const float nameWidth = layout.closeButton.left - 4 - (left + 16 + 6 + layout.titleWidth + 12);
    layout.deviceName = TruncateMiddle(model.OutputDeviceName(), kDeviceStyle, nameWidth);
    y += kHeaderHeight;

    if (!layout.error.empty()) {
        y += kSectionSpacing;
        const float height = Measure(layout.error, kWarningStyle, right - left - kWarningIndent).height;
        layout.errorRect = {left, y, right, y + height};
        y += height;
    }

    y += kSectionSpacing;
    const auto row = [&](float height) {
        const D2D1_RECT_F rect{left, y, right, y + height};
        y += height + kRowSpacing;
        return rect;
    };
    if (layout.showsLoudness) layout.shortTermRow = row(kLoudnessRowHeight);
    layout.leftRow = row(kBarHeight);
    layout.rightRow = row(kBarHeight);
    if (layout.showsLoudness) layout.integratedRow = row(kLoudnessRowHeight);
    layout.scaleRow = {left, y, right, y + kScaleHeight};
    y += kScaleHeight;
    if (layout.showsLoudness) {
        y += kRowSpacing;
        const float height = Measure(loudnessCaption_, kCaptionStyle, right - left).height;
        layout.caption = {left, y, right, y + height};
        y += height + kRowSpacing;
        layout.keepMeasuring = CheckboxRect(left, y, kCheckboxRowHeight, kKeepMeasuringLabel);
        y += kCheckboxRowHeight;
    }

    y += kSectionSpacing;
    layout.dividerY = y;
    y += 1 + kSectionSpacing;

    layout.loudnessToggle = CheckboxRect(left, y, kControlHeight, kLoudnessLabel);
    layout.launchToggle = CheckboxRect(layout.loudnessToggle.right + 16, y, kControlHeight, kLaunchAtLoginLabel);
    const float buttonWidth = std::ceil(Measure(kQuitLabel, kButtonStyle).width) + 24;
    layout.quitButton = {right - buttonWidth, y, right, y + kControlHeight};
    y += kControlHeight;

    if (login.DisabledInSettings()) {
        layout.note = kDisabledNote;
        layout.hasLink = true;
    } else if (login.RegisteredElsewhere()) {
        layout.note = kOtherCopyNote;
    }
    if (!layout.note.empty()) {
        y += kRowSpacing;
        const float width = std::ceil(Measure(layout.note, kNoteStyle).width);
        layout.noteRect = {left, y, left + width, y + kNoteHeight};
        if (layout.hasLink) {
            const float linkLeft = layout.noteRect.right + 5;
            const float linkWidth = std::ceil(Measure(kOpenSettingsLabel, kNoteStyle).width);
            layout.linkRect = {linkLeft, y, linkLeft + linkWidth, y + kNoteHeight};
        }
        y += kNoteHeight;
    }

    layout.launchError = login.ErrorMessage();
    if (!layout.launchError.empty()) {
        y += kRowSpacing;
        const float height = Measure(layout.launchError, kWarningStyle, right - left - kWarningIndent).height;
        layout.launchErrorRect = {left, y, right, y + height};
        y += height;
    }

    layout.height = y + kPadding;
    layout_ = layout;

    const std::vector<Control> order = FocusOrder();
    if (std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = Control::None;
    RebuildTooltips();
}

void MeterWindow::SetDpi(UINT dpi) {
    if (dpi == 0 || dpi == dpi_) return;
    dpi_ = dpi;
    if (target_) target_->SetDpi(float(dpi), float(dpi));
    RebuildTooltips();
}

// Puts the flyout where Windows puts its own: just inside the taskbar, centred on the tray icon,
// or in the corner by the clock when the icon is tucked away in the overflow menu.
void MeterWindow::Place() {
    RECT anchor = iconRect_;
    if (!hasIcon_) {
        const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!taskbar || !GetWindowRect(taskbar, &anchor)) anchor = {0, 0, 1, 1};
    }
    const HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    SetDpi(dpiX);

    const RECT& work = info.rcWork;
    UINT edge = ABE_BOTTOM;
    RECT bar = {work.left, work.bottom, work.right, work.bottom};
    APPBARDATA appBar = {};
    appBar.cbSize = sizeof(appBar);
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &appBar) &&
        MonitorFromRect(&appBar.rc, MONITOR_DEFAULTTONULL) == monitor) {
        edge = appBar.uEdge;
        bar = appBar.rc;
    }

    const auto place = [&] {
        const int width = Pixels(kWidth);
        const int height = Pixels(layout_.height);
        const int margin = Pixels(kScreenMargin);
        const int centerX = (anchor.left + anchor.right) / 2;
        const int centerY = (anchor.top + anchor.bottom) / 2;
        int x = hasIcon_ ? centerX - width / 2 : work.right - width - margin;
        int y = hasIcon_ ? centerY - height / 2 : work.bottom - height - margin;
        switch (edge) {
        case ABE_TOP: y = std::max(work.top, bar.bottom) + margin; break;
        case ABE_LEFT: x = std::max(work.left, bar.right) + margin; break;
        case ABE_RIGHT: x = std::min(work.right, bar.left) - width - margin; break;
        default: y = std::min(work.bottom, bar.top) - height - margin; break;
        }
        x = std::max(int(work.left) + margin, std::min(x, int(work.right) - width - margin));
        y = std::max(int(work.top) + margin, std::min(y, int(work.bottom) - height - margin));
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    };

    edge_ = edge;
    positioning_ = true;
    place();
    // Moving to a monitor with another scale factor changes the window's DPI under us.
    const UINT actual = GetDpiForWindow(hwnd_);
    if (actual && actual != dpi_) {
        SetDpi(actual);
        place();
    }
    positioning_ = false;
}

// Fits the window to its layout again, keeping the edge by the taskbar where it is.
void MeterWindow::Resize() {
    RECT window;
    GetWindowRect(hwnd_, &window);
    const int width = Pixels(kWidth);
    const int height = Pixels(layout_.height);
    if (width == window.right - window.left && height == window.bottom - window.top) return;
    const int y = edge_ == ABE_TOP ? window.top : window.bottom - height;
    SetWindowPos(hwnd_, nullptr, window.left, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MeterWindow::RebuildTooltips() {
    if (!tooltip_) return;
    for (LRESULT count = SendMessageW(tooltip_, TTM_GETTOOLCOUNT, 0, 0); count > 0; --count) {
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        if (!SendMessageW(tooltip_, TTM_ENUMTOOLSW, 0, LPARAM(&tool))) break;
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, LPARAM(&tool));
    }
    SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Pixels(300));

    UINT_PTR nextId = 1;
    const auto add = [&](const D2D1_RECT_F& rect, const wchar_t* text) {
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = hwnd_;
        tool.uId = nextId++;
        tool.rect = ToPixels(rect);
        tool.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, LPARAM(&tool));
    };
    const auto addChannel = [&](const D2D1_RECT_F& row) {
        const D2D1_RECT_F target = RowTarget(row);
        const Columns columns = ColumnsFor(row);
        const float clipLeft = columns.clipLeft - kColumnSpacing / 2;
        const float clipRight = columns.clipLeft + kClipIndicatorSize + kColumnSpacing / 2;
        add({target.left, target.top, clipLeft, target.bottom}, kPeakHelp);
        add({clipLeft, target.top, clipRight, target.bottom}, kClipHelp);
        add({clipRight, target.top, target.right, target.bottom}, kPeakHelp);
    };
    addChannel(layout_.leftRow);
    addChannel(layout_.rightRow);
    if (layout_.showsLoudness) {
        add(RowTarget(layout_.shortTermRow), shortTermHelp_.c_str());
        add(RowTarget(layout_.integratedRow), integratedHelp_.c_str());
        add(layout_.keepMeasuring, kKeepMeasuringHelp);
    }
    add(layout_.loudnessToggle, kLoudnessHelp);
    add(layout_.closeButton, kCloseHelp);
}

// MARK: Input

MeterWindow::Control MeterWindow::HitTest(float x, float y) const {
    const Layout& l = layout_;
    if (Contains(l.closeButton, x, y)) return Control::Close;
    if (l.showsLoudness && Contains(l.keepMeasuring, x, y)) return Control::KeepMeasuring;
    if (Contains(l.loudnessToggle, x, y)) return Control::Loudness;
    if (Contains(l.launchToggle, x, y)) return Control::LaunchAtLogin;
    if (Contains(l.quitButton, x, y)) return Control::Quit;
    if (l.hasLink && Contains(l.linkRect, x, y)) return Control::OpenSettings;
    if (Contains(RowTarget(l.leftRow), x, y) || Contains(RowTarget(l.rightRow), x, y)) return Control::Meters;
    if (l.showsLoudness &&
        (Contains(RowTarget(l.shortTermRow), x, y) || Contains(RowTarget(l.integratedRow), x, y))) {
        return Control::Meters;
    }
    return Control::None;
}

D2D1_RECT_F MeterWindow::ControlRect(Control control) const {
    switch (control) {
    case Control::KeepMeasuring: return layout_.keepMeasuring;
    case Control::Loudness: return layout_.loudnessToggle;
    case Control::LaunchAtLogin: return layout_.launchToggle;
    case Control::Quit: return layout_.quitButton;
    case Control::OpenSettings: return layout_.linkRect;
    default: return {};
    }
}

std::vector<MeterWindow::Control> MeterWindow::FocusOrder() const {
    std::vector<Control> order;
    if (layout_.showsLoudness) order.push_back(Control::KeepMeasuring);
    order.push_back(Control::Loudness);
    order.push_back(Control::LaunchAtLogin);
    order.push_back(Control::Quit);
    if (layout_.hasLink) order.push_back(Control::OpenSettings);
    return order;
}

void MeterWindow::MoveFocus(int step) {
    const std::vector<Control> order = FocusOrder();
    const int count = int(order.size());
    const auto current = std::find(order.begin(), order.end(), focus_);
    int index = step > 0 ? 0 : count - 1;
    if (current != order.end()) index = (int(current - order.begin()) + step + count) % count;
    focus_ = order[index];
    showsFocus_ = true;
    Invalidate();
}

void MeterWindow::Activate(Control control) {
    Settings& settings = app_.Preferences();
    switch (control) {
    case Control::Close:
        Hide();
        break;
    case Control::Meters:
        app_.Model().ResetPeaks();
        Invalidate();
        break;
    case Control::KeepMeasuring:
        settings.SetMeasuresInBackground(!settings.MeasuresInBackground());
        Invalidate();
        break;
    case Control::Loudness:
        settings.SetShowsLoudness(!settings.ShowsLoudness());
        Update();
        break;
    case Control::LaunchAtLogin:
        app_.Login().SetEnabled(!app_.Login().IsEnabled());
        Update();
        break;
    case Control::Quit:
        app_.Quit();
        break;
    case Control::OpenSettings:
        LaunchAtLogin::OpenSettings();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK MeterWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MeterWindow* self;
    if (message == WM_NCCREATE) {
        self = static_cast<MeterWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MeterWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MeterWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hwnd_, &paint);
        Render();
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (target_) target_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        return 0;
    case WM_DPICHANGED:
        SetDpi(HIWORD(wParam));
        if (!positioning_) Resize();
        return 0;
    case WM_ACTIVATE:
        // The flyout stays open when another window takes the focus; the focus ring goes.
        if (LOWORD(wParam) == WA_INACTIVE && showsFocus_) {
            showsFocus_ = false;
            Invalidate();
        }
        break;
    case WM_MOUSEMOVE: {
        const Control control = HitTest(GET_X_LPARAM(lParam) / Scale(), GET_Y_LPARAM(lParam) / Scale());
        if (control != hover_) {
            hover_ = control;
            Invalidate();
        }
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hover_ = Control::None;
        Invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = HitTest(GET_X_LPARAM(lParam) / Scale(), GET_Y_LPARAM(lParam) / Scale());
        showsFocus_ = false;
        if (pressed_ != Control::None) SetCapture(hwnd_);
        Invalidate();
        return 0;
    case WM_LBUTTONUP: {
        const Control pressed = pressed_;
        pressed_ = Control::None;
        if (GetCapture() == hwnd_) ReleaseCapture();
        Invalidate();
        if (pressed != Control::None &&
            pressed == HitTest(GET_X_LPARAM(lParam) / Scale(), GET_Y_LPARAM(lParam) / Scale())) {
            Activate(pressed);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        pressed_ = Control::None;
        Invalidate();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && hover_ == Control::OpenSettings) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE:
            Hide();
            return 0;
        case VK_TAB:
            MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
            return 0;
        case VK_SPACE:
        case VK_RETURN:
            if (focus_ != Control::None) Activate(focus_);
            return 0;
        }
        break;
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_DESTROY:
        ReleaseGraphics();
        tooltip_ = nullptr;
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

// MARK: Text

IDWriteTextFormat* MeterWindow::Format(const TextStyle& style) {
    for (const CachedFormat& cached : formats_) {
        if (cached.size == style.size && cached.weight == style.weight && cached.icon == style.icon) {
            return cached.format.Get();
        }
    }
    const std::wstring& family = style.icon ? iconFamily_ : style.size < 12 ? smallTextFamily_ : textFamily_;
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(family.c_str(), nullptr, style.weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, style.size, locale_, &format))) {
        return nullptr;
    }
    formats_.push_back({style.size, style.weight, style.icon, format});
    return format.Get();
}

ComPtr<IDWriteTextLayout> MeterWindow::MakeLayout(const std::wstring& text, const TextStyle& style,
                                                  float width, float height) {
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat* format = Format(style);
    if (!format || FAILED(dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), format,
                                                    std::max(width, 0.0f), std::max(height, 0.0f), &layout))) {
        return nullptr;
    }
    layout->SetTextAlignment(style.alignment);
    layout->SetWordWrapping(style.wraps ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetParagraphAlignment(style.wraps ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (style.tabularFigures && tabularFigures_) {
        layout->SetTypography(tabularFigures_.Get(), {0, UINT32(text.size())});
    }
    return layout;
}

D2D1_SIZE_F MeterWindow::Measure(const std::wstring& text, const TextStyle& style, float maxWidth) {
    TextStyle leading = style;
    leading.alignment = kLeading;
    const ComPtr<IDWriteTextLayout> layout = MakeLayout(text, leading, maxWidth, 10000.0f);
    DWRITE_TEXT_METRICS metrics = {};
    if (layout) layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

void MeterWindow::DrawString(const std::wstring& text, const D2D1_RECT_F& rect, const TextStyle& style,
                             const D2D1_COLOR_F& color) {
    const ComPtr<IDWriteTextLayout> layout = MakeLayout(text, style, rect.right - rect.left, rect.bottom - rect.top);
    if (!layout) return;
    brush_->SetColor(color);
    target_->DrawTextLayout({rect.left, rect.top}, layout.Get(), brush_.Get());
}

// Keeps both ends of a long device name, as "Speakers (Realtek…Audio)".
std::wstring MeterWindow::TruncateMiddle(const std::wstring& text, const TextStyle& style, float maxWidth) {
    if (text.empty() || maxWidth <= 0) return {};
    if (Measure(text, style).width <= maxWidth) return text;
    std::wstring best;
    size_t low = 0, high = text.size() - 1;  // characters kept
    while (low < high) {
        const size_t keep = (low + high + 1) / 2;
        const size_t head = (keep + 1) / 2;
        const std::wstring candidate = text.substr(0, head) + kEllipsis + text.substr(text.size() - keep / 2);
        if (Measure(candidate, style).width <= maxWidth) {
            best = candidate;
            low = keep;
        } else {
            high = keep - 1;
        }
    }
    return best;
}

// MARK: Drawing

// Direct2D's factory keeps a GPU device alive, which is most of the app's memory. It only exists
// while the flyout is open.
void MeterWindow::ReleaseGraphics() {
    brush_.Reset();
    target_.Reset();
    d2d_.Reset();
}

bool MeterWindow::EnsureTarget() {
    if (target_) return true;
    if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    RECT client;
    GetClientRect(hwnd_, &client);
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), float(dpi_), float(dpi_));
    const auto windowProperties = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(client.right, client.bottom), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    if (FAILED(d2d_->CreateHwndRenderTarget(properties, windowProperties, &target_))) return false;
    return SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_));
}

void MeterWindow::FillRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
    brush_->SetColor(color);
    target_->FillRectangle(rect, brush_.Get());
}

void MeterWindow::FillRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color) {
    brush_->SetColor(color);
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
}

void MeterWindow::StrokeRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color,
                                    float width) {
    brush_->SetColor(color);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get(), width);
}

void MeterWindow::Render() {
    if (!hwnd_ || !EnsureTarget()) return;
    const Palette& colors = app_.Colors();
    const LevelMeterModel& model = app_.Model();
    const Layout& l = layout_;

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(colors.background);

    DrawHeader();
    DrawCloseButton();
    if (!l.error.empty()) DrawWarning(l.errorRect, l.error);

    if (l.showsLoudness) DrawLoudnessRow(l.shortTermRow, L"S", model.Reading().shortTerm);
    DrawChannelRow(l.leftRow, L"L", model.ChannelAt(0));
    DrawChannelRow(l.rightRow, L"R", model.ChannelAt(1));
    if (l.showsLoudness) DrawLoudnessRow(l.integratedRow, L"I", model.Reading().integrated);
    DrawScale(l.scaleRow);
    if (l.showsLoudness) {
        DrawString(loudnessCaption_, l.caption, kCaptionStyle, colors.textSecondary);
        DrawCheckbox(l.keepMeasuring, kKeepMeasuringLabel, app_.Preferences().MeasuresInBackground(),
                     Control::KeepMeasuring);
    }

    const float dividerTop = Snap(l.dividerY);
    FillRect({kPadding, dividerTop, kWidth - kPadding, dividerTop + 1 / Scale()}, colors.divider);

    DrawCheckbox(l.loudnessToggle, kLoudnessLabel, l.showsLoudness, Control::Loudness);
    DrawCheckbox(l.launchToggle, kLaunchAtLoginLabel, app_.Login().IsEnabled(), Control::LaunchAtLogin);
    DrawButton(l.quitButton, kQuitLabel, Control::Quit);
    if (!l.note.empty()) {
        D2D1_RECT_F noteRect = l.noteRect;
        noteRect.right += 4;
        DrawString(l.note, noteRect, kNoteStyle, colors.textSecondary);
    }
    if (l.hasLink) {
        const bool hovered = hover_ == Control::OpenSettings;
        const float opacity = hovered && pressed_ == Control::OpenSettings ? 0.6f : hovered ? 0.8f : 1.0f;
        D2D1_RECT_F linkRect = l.linkRect;
        linkRect.right += 4;
        DrawString(kOpenSettingsLabel, linkRect, kNoteStyle, WithOpacity(colors.accentText, opacity));
    }
    if (!l.launchError.empty()) DrawWarning(l.launchErrorRect, l.launchError);
    DrawFocusRing();

    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        ReleaseGraphics();
        Invalidate();
    }
}

void MeterWindow::DrawHeader() {
    const Palette& colors = app_.Colors();
    const D2D1_RECT_F& header = layout_.header;
    const float centerY = CenterY(header);
    DrawString(kSpeakerGlyph, Centered(header.left, header.left + 20, centerY), kHeaderIconStyle,
               colors.textSecondary);
    const float titleLeft = header.left + 16 + 6;
    DrawString(kTitle, Centered(titleLeft, titleLeft + layout_.titleWidth + 4, centerY), kTitleStyle,
               colors.textPrimary);
    if (!layout_.deviceName.empty()) {
        DrawString(layout_.deviceName,
                   Centered(titleLeft + layout_.titleWidth + 8, layout_.closeButton.left - 4, centerY),
                   kDeviceStyle, colors.textSecondary);
    }
}

void MeterWindow::DrawWarning(const D2D1_RECT_F& rect, const std::wstring& text) {
    const Palette& colors = app_.Colors();
    DrawString(kWarningGlyph, {rect.left, rect.top, rect.left + kWarningIndent, rect.top + 16},
               kWarningIconStyle, colors.orange);
    DrawString(text, {rect.left + kWarningIndent, rect.top, rect.right, rect.bottom}, kWarningStyle,
               colors.orange);
}

void MeterWindow::DrawChannelRow(const D2D1_RECT_F& row, const wchar_t* name,
                                 const LevelMeterModel::Channel& channel) {
    const Palette& colors = app_.Colors();
    const Columns columns = ColumnsFor(row);
    const float centerY = CenterY(row);
    DrawString(name, Centered(columns.label - 4, columns.label + kLabelWidth + 4, centerY), kChannelLabelStyle,
               colors.textSecondary);
    DrawSegmentedBar({columns.barLeft, row.top, columns.barRight, row.bottom}, channel.level, channel.peakHold);
    brush_->SetColor(channel.clipped ? colors.red : WithOpacity(colors.textSecondary, 0.25f));
    const float radius = kClipIndicatorSize / 2;
    target_->FillEllipse(D2D1::Ellipse({columns.clipLeft + radius, centerY}, radius, radius), brush_.Get());
    DrawString(FormatReadout(channel.maxPeak), Centered(columns.readoutLeft, columns.right, centerY),
               kChannelReadoutStyle, channel.clipped ? colors.red : colors.textPrimary);
}

// A thin continuous loudness bar on the same scale as the peak bars, with a target marker.
void MeterWindow::DrawLoudnessRow(const D2D1_RECT_F& row, const wchar_t* label, float value) {
    const Palette& colors = app_.Colors();
    const Columns columns = ColumnsFor(row);
    const float centerY = CenterY(row);
    DrawString(label, Centered(columns.label - 4, columns.label + kLabelWidth + 4, centerY), kLoudnessLabelStyle,
               colors.textSecondary);
    DrawLoudnessBar({columns.barLeft, centerY - kLoudnessBarHeight / 2, columns.barRight,
                     centerY + kLoudnessBarHeight / 2}, value);
    DrawString(FormatReadout(value), Centered(columns.readoutLeft, columns.right, centerY),
               kLoudnessReadoutStyle, colors.textPrimary);
}

// LED-style bar: lit segments up to the current level plus a single lit segment at the peak hold.
// Segments are laid out on whole pixels so they stay crisp at every scale factor.
void MeterWindow::DrawSegmentedBar(const D2D1_RECT_F& rect, float level, float peakHold) {
    const Palette& colors = app_.Colors();
    const float scale = Scale();
    const float left = std::round(rect.left * scale);
    const float top = std::round(rect.top * scale);
    const float bottom = std::round(rect.bottom * scale);
    const float width = std::round(rect.right * scale) - left;
    const float gap = std::max(1.0f, std::round(kSegmentGap * scale));
    const float segment = std::max(1.0f, std::round(kSegmentWidth * scale));
    const int count = int((width + gap) / (segment + gap));
    if (count <= 0) return;
    const float pitch = (width + gap) / float(count);
    const float levelPosition = MeterScale::Position(level);
    const float holdPosition = MeterScale::Position(peakHold);
    const int holdIndex = holdPosition > 0 ? std::min(int(holdPosition * float(count)), count - 1) : -1;

    for (int index = 0; index < count; ++index) {
        const float position = float(index) / float(count);
        const bool lit = position < levelPosition || index == holdIndex;
        const float x0 = left + std::round(float(index) * pitch);
        const float x1 = left + std::round(float(index + 1) * pitch) - gap;
        FillRoundedRect({x0 / scale, top / scale, x1 / scale, bottom / scale}, 1,
                        WithOpacity(ZoneColor(colors, position), lit ? 1.0f : kUnlitOpacity));
    }
}

void MeterWindow::DrawLoudnessBar(const D2D1_RECT_F& rect, float value) {
    const Palette& colors = app_.Colors();
    const float width = rect.right - rect.left;
    const float radius = (rect.bottom - rect.top) / 2;
    const D2D1_ROUNDED_RECT track = D2D1::RoundedRect(rect, radius, radius);
    brush_->SetColor(WithOpacity(colors.textSecondary, kUnlitOpacity));
    target_->FillRoundedRectangle(track, brush_.Get());

    const float valueX = rect.left + MeterScale::Position(value) * width;
    const float targetX = rect.left + MeterScale::Position(Loudness::kTarget) * width;
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    if (valueX > rect.left && SUCCEEDED(d2d_->CreateRoundedRectangleGeometry(track, &mask))) {
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), nullptr);
        FillRect({rect.left, rect.top, std::min(valueX, targetX), rect.bottom}, colors.teal);
        // Past the target the bar turns orange.
        if (valueX > targetX) FillRect({targetX, rect.top, valueX, rect.bottom}, colors.orange);
        target_->PopLayer();
    }
    FillRect({targetX - 0.75f, rect.top, targetX + 0.75f, rect.bottom}, colors.textPrimary);
}

void MeterWindow::DrawScale(const D2D1_RECT_F& row) {
    const Palette& colors = app_.Colors();
    const Columns columns = ColumnsFor(row);
    const float centerY = CenterY(row);
    const float width = columns.barRight - columns.barLeft;
    const float first = MeterScale::kTicks[0];
    for (float tick : MeterScale::kTicks) {
        const float x = columns.barLeft + MeterScale::Position(tick) * width;
        TextStyle style = kTickStyle;
        D2D1_RECT_F rect = Centered(x - 20, x + 20, centerY, 16);
        style.alignment = kCenter;
        if (tick == first) {
            style.alignment = kLeading;
            rect = Centered(x, x + 40, centerY, 16);
        } else if (tick == 0) {
            style.alignment = kTrailing;
            rect = Centered(x - 40, x, centerY, 16);
        }
        DrawString(std::to_wstring(int(tick)), rect, style, colors.textSecondary);
    }
    DrawString(L"dB", Centered(columns.readoutLeft, columns.right, centerY, 16), kUnitStyle, colors.textSecondary);
}

void MeterWindow::DrawCheckbox(const D2D1_RECT_F& rect, const wchar_t* label, bool checked, Control control) {
    const Palette& colors = app_.Colors();
    const float centerY = CenterY(rect);
    const bool hovered = hover_ == control;
    const bool pressed = hovered && pressed_ == control;
    const D2D1_RECT_F box = {rect.left, centerY - kCheckboxSize / 2, rect.left + kCheckboxSize,
                             centerY + kCheckboxSize / 2};
    if (checked) {
        FillRoundedRect(box, kControlRadius, WithOpacity(colors.accentFill, pressed ? 0.8f : hovered ? 0.9f : 1.0f));
        DrawString(kCheckGlyph, box, kCheckIconStyle, colors.onAccent);
    } else {
        const D2D1_RECT_F inset = {box.left + 0.5f, box.top + 0.5f, box.right - 0.5f, box.bottom - 0.5f};
        FillRoundedRect(inset, kControlRadius,
                        pressed ? colors.checkFillPressed : hovered ? colors.checkFillHover : colors.checkFill);
        StrokeRoundedRect(inset, kControlRadius, colors.checkStroke, 1);
    }
    DrawString(label, Centered(box.right + kCheckboxLabelGap, rect.right + 4, centerY), kControlStyle,
               colors.textPrimary);
}

void MeterWindow::DrawButton(const D2D1_RECT_F& rect, const wchar_t* label, Control control) {
    const Palette& colors = app_.Colors();
    const bool hovered = hover_ == control;
    const bool pressed = hovered && pressed_ == control;
    const D2D1_RECT_F inset = {rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f};
    FillRoundedRect(inset, kControlRadius,
                    pressed ? colors.buttonFillPressed : hovered ? colors.buttonFillHover : colors.buttonFill);
    StrokeRoundedRect(inset, kControlRadius, colors.buttonStroke, 1);
    DrawString(label, rect, kButtonStyle, pressed ? colors.textSecondary : colors.textPrimary);
}

// A borderless button like the close buttons of Windows 11 flyouts: a fill only under the mouse.
void MeterWindow::DrawCloseButton() {
    const Palette& colors = app_.Colors();
    const bool hovered = hover_ == Control::Close;
    const bool pressed = hovered && pressed_ == Control::Close;
    if (hovered) {
        FillRoundedRect(layout_.closeButton, kControlRadius,
                        pressed ? colors.subtleFillPressed : colors.subtleFillHover);
    }
    DrawString(kCloseGlyph, layout_.closeButton, kCloseIconStyle,
               hovered && !pressed ? colors.textPrimary : colors.textSecondary);
}

void MeterWindow::DrawFocusRing() {
    if (!showsFocus_ || focus_ == Control::None) return;
    const D2D1_RECT_F rect = ControlRect(focus_);
    StrokeRoundedRect({rect.left - 3, rect.top - 1, rect.right + 3, rect.bottom + 1}, 6,
                      app_.Colors().textPrimary, 2);
}

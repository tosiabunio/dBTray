#include "Theme.h"

#include <windows.h>

namespace {

D2D1_COLOR_F Rgb(unsigned rgb, float alpha = 1.0f) {
    return D2D1::ColorF(rgb, alpha);
}

// The system accent ramp, lightest (Light3) to darkest (Dark3), as Explorer stores it.
struct AccentRamp {
    unsigned light3 = 0x99EBFF, light2 = 0x4CC2FF, dark1 = 0x0067C0, dark2 = 0x003E92;
};

AccentRamp ReadAccentRamp() {
    AccentRamp ramp;
    BYTE palette[32];
    DWORD size = sizeof(palette);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS &&
        size == sizeof(palette)) {
        const auto entry = [&](int index) {
            const BYTE* p = palette + index * 4;  // R, G, B, unused
            return unsigned(p[0]) << 16 | unsigned(p[1]) << 8 | unsigned(p[2]);
        };
        ramp.light3 = entry(0);
        ramp.light2 = entry(1);
        ramp.dark1 = entry(4);
        ramp.dark2 = entry(5);
    }
    return ramp;
}

}  // namespace

bool SystemUsesLightTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}

D2D1_COLOR_F WithOpacity(D2D1_COLOR_F color, float opacity) {
    color.a *= opacity;
    return color;
}

Palette MakePalette() {
    const AccentRamp accent = ReadAccentRamp();
    Palette p;
    p.dark = !SystemUsesLightTheme();
    if (p.dark) {
        p.background = Rgb(0x2C2C2C);
        p.textPrimary = Rgb(0xFFFFFF);
        p.textSecondary = Rgb(0xFFFFFF, 0.786f);
        p.divider = Rgb(0xFFFFFF, 0.0837f);
        p.green = Rgb(0x30D158);
        p.yellow = Rgb(0xFFD60A);
        p.red = Rgb(0xFF453A);
        p.teal = Rgb(0x40C8E0);
        p.orange = Rgb(0xFF9F0A);
        p.accentFill = Rgb(accent.light2);
        p.onAccent = Rgb(0x000000);
        p.accentText = Rgb(accent.light3);
        p.checkFill = Rgb(0x000000, 0.1f);
        p.checkFillHover = Rgb(0xFFFFFF, 0.0419f);
        p.checkFillPressed = Rgb(0xFFFFFF, 0.0698f);
        p.checkStroke = Rgb(0xFFFFFF, 0.5442f);
        p.buttonFill = Rgb(0xFFFFFF, 0.0605f);
        p.buttonFillHover = Rgb(0xFFFFFF, 0.0837f);
        p.buttonFillPressed = Rgb(0xFFFFFF, 0.0326f);
        p.buttonStroke = Rgb(0xFFFFFF, 0.093f);
    } else {
        p.background = Rgb(0xF9F9F9);
        p.textPrimary = Rgb(0x000000, 0.8956f);
        p.textSecondary = Rgb(0x000000, 0.6063f);
        p.divider = Rgb(0x000000, 0.0803f);
        p.green = Rgb(0x34C759);
        p.yellow = Rgb(0xFFCC00);
        p.red = Rgb(0xFF3B30);
        p.teal = Rgb(0x30B0C7);
        p.orange = Rgb(0xFF9500);
        p.accentFill = Rgb(accent.dark1);
        p.onAccent = Rgb(0xFFFFFF);
        p.accentText = Rgb(accent.dark2);
        p.checkFill = Rgb(0x000000, 0.0241f);
        p.checkFillHover = Rgb(0x000000, 0.0578f);
        p.checkFillPressed = Rgb(0x000000, 0.0924f);
        p.checkStroke = Rgb(0x000000, 0.4458f);
        p.buttonFill = Rgb(0xFFFFFF, 0.7f);
        p.buttonFillHover = Rgb(0xF9F9F9, 0.5f);
        p.buttonFillPressed = Rgb(0xF9F9F9, 0.3f);
        p.buttonStroke = Rgb(0x000000, 0.1f);
    }
    return p;
}

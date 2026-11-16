#pragma once

#include <d2d1.h>

// Colours of the meter flyout for the current Windows theme and accent colour.
struct Palette {
    bool dark = true;
    D2D1_COLOR_F background, textPrimary, textSecondary, divider;
    // Meter colours.
    D2D1_COLOR_F green, yellow, red, teal, orange;
    // Controls.
    D2D1_COLOR_F accentFill, onAccent, accentText;
    D2D1_COLOR_F checkFill, checkFillHover, checkFillPressed, checkStroke;
    D2D1_COLOR_F buttonFill, buttonFillHover, buttonFillPressed, buttonStroke;
    D2D1_COLOR_F subtleFillHover, subtleFillPressed;  // borderless buttons, like the close button
};

// "Windows mode" in Settings › Personalization › Colors: the theme of the taskbar and its flyouts.
bool SystemUsesLightTheme();

// Follows Windows mode, like the system's own tray flyouts.
Palette MakePalette();

D2D1_COLOR_F WithOpacity(D2D1_COLOR_F color, float opacity);

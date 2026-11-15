#pragma once

#include <algorithm>
#include <cmath>

// Maps dBFS values onto the meter bar and its colour zones.
namespace MeterScale {

// Lowest level the meter shows; anything quieter reads as silence.
constexpr float kFloor = -60.0f;
constexpr float kTicks[] = {-60, -40, -30, -20, -10, -6, -3, 0};

enum class Zone { Green, Yellow, Red };

inline float IecDeflection(float db) {
    if (db < -70) return 0;
    if (db < -60) return (db + 70) * 0.0025f;
    if (db < -50) return (db + 60) * 0.005f + 0.025f;
    if (db < -40) return (db + 50) * 0.0075f + 0.075f;
    if (db < -30) return (db + 40) * 0.015f + 0.15f;
    if (db < -20) return (db + 30) * 0.02f + 0.3f;
    if (db < 0) return (db + 20) * 0.025f + 0.5f;
    return 1;
}

inline float DecibelsFromAmplitude(float amplitude) {
    return amplitude > 0 ? std::max(20 * std::log10(amplitude), kFloor) : kFloor;
}

// Bar position in 0...1 using the IEC 60268-18 deflection curve, which gives more room to the
// loud end of the scale, rescaled so that kFloor sits at 0.
inline float Position(float db) {
    const float bottom = IecDeflection(kFloor);
    return std::max(0.0f, (IecDeflection(db) - bottom) / (1 - bottom));
}

// Green below -12 dB, yellow from -12 to -3 dB, red above -3 dB.
inline Zone ZoneAt(float position) {
    static const float warning = Position(-12);
    static const float danger = Position(-3);
    if (position >= danger) return Zone::Red;
    if (position >= warning) return Zone::Yellow;
    return Zone::Green;
}

}  // namespace MeterScale

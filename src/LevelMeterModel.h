#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "AudioCapture.h"
#include "Loudness.h"
#include "MeterScale.h"

// Polls the loopback capture at display rate and applies meter ballistics for the UI.
class LevelMeterModel {
public:
    struct Channel {
        // Displayed level in dBFS: instant attack, linear release.
        float level = MeterScale::kFloor;
        // Highest recent level in dBFS, held briefly before falling back.
        float peakHold = MeterScale::kFloor;
        // Highest level in dBFS since the last ResetPeaks().
        float maxPeak = MeterScale::kFloor;
        // Latched once the signal reaches 0 dBFS; cleared by ResetPeaks().
        bool clipped = false;
        double peakHoldTime = 0;
    };

    // BS.1770 loudness of the stereo mix, in LUFS (MeterScale::kFloor when silent).
    struct LoudnessReading {
        // Loudness of the last 3 s.
        float shortTerm = MeterScale::kFloor;
        // Gated loudness since measuring started or was reset.
        float integrated = MeterScale::kFloor;
    };

    // The refresh timer and capture failures are delivered to `window`.
    void Attach(HWND window) { window_ = window; }

    // Starts capturing, or carries on if capture kept running while the meter was closed.
    void Start();
    // While the meter is hidden, refresh just often enough to keep up with the 100 ms loudness
    // blocks instead of at display rate.
    void SetVisible(bool visible);
    void Stop();
    // Clears the peak hold, maximum peak and clip indicator of every channel, and starts
    // integrated loudness over.
    void ResetPeaks();
    // Advances the meter; returns whether anything it shows changed.
    bool Tick();

    // Reopens capture on the current default device, keeping the loudness measured so far.
    void Restart();
    void OnDefaultDeviceChanged();
    void OnCaptureFailed(LPARAM generation);

    bool IsCapturing() const { return capture_.IsRunning(); }
    const Channel& ChannelAt(int index) const { return channels_[index]; }
    const LoudnessReading& Reading() const { return reading_; }
    const std::wstring& OutputDeviceName() const { return outputDeviceName_; }
    const std::wstring& ErrorMessage() const { return errorMessage_; }

private:
    bool StartCapture();
    void ScheduleTimer();

    HWND window_ = nullptr;
    AudioCapture capture_;
    LoudnessMeter loudnessMeter_;
    Channel channels_[2];
    LoudnessReading reading_;
    std::wstring outputDeviceName_;
    std::wstring errorMessage_;
    std::vector<double> blocks_;
    double lastTick_ = 0;
    LPARAM generation_ = 0;
    bool timerRunning_ = false;
    bool visible_ = true;
    // Capture is wanted: the meter is open, or it measures in the background.
    bool shouldRun_ = false;
};

#include "LevelMeterModel.h"

#include "AppMessages.h"

#include <algorithm>

namespace {

constexpr float kReleaseRate = 24;         // dB per second
constexpr double kPeakHoldDuration = 1.5;  // s
// With Windows' default 15.6 ms timer resolution this fires about 64 times a second.
constexpr UINT kRefreshInterval = 15;             // ms
constexpr UINT kBackgroundRefreshInterval = 100;  // ms
constexpr UINT kRestartDelay = 500;               // ms

double Now() {
    static const double frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return double(value.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return double(counter.QuadPart) / frequency;
}

bool LooksTheSame(const LevelMeterModel::Channel& a, const LevelMeterModel::Channel& b) {
    return a.level == b.level && a.peakHold == b.peakHold && a.maxPeak == b.maxPeak &&
           a.clipped == b.clipped;
}

}  // namespace

void LevelMeterModel::Start() {
    shouldRun_ = true;
    outputDeviceName_ = AudioCapture::DefaultOutputDeviceName();
    if (timerRunning_) return;
    if (!StartCapture()) return;
    lastTick_ = Now();
    ScheduleTimer();
}

void LevelMeterModel::SetVisible(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    if (timerRunning_) ScheduleTimer();
}

bool LevelMeterModel::StartCapture() {
    std::wstring error;
    if (!capture_.Start(window_, WM_APP_CAPTURE_FAILED, ++generation_, error)) {
        errorMessage_ = error;
        return false;
    }
    errorMessage_.clear();
    return true;
}

void LevelMeterModel::ScheduleTimer() {
    SetTimer(window_, kTickTimer, visible_ ? kRefreshInterval : kBackgroundRefreshInterval, nullptr);
    timerRunning_ = true;
}

void LevelMeterModel::Stop() {
    shouldRun_ = false;
    KillTimer(window_, kTickTimer);
    KillTimer(window_, kRestartTimer);
    timerRunning_ = false;
    capture_.Stop();
    channels_[0] = channels_[1] = Channel();
    loudnessMeter_ = LoudnessMeter();
    reading_ = LoudnessReading();
}

void LevelMeterModel::ResetPeaks() {
    for (Channel& channel : channels_) {
        channel.peakHold = MeterScale::kFloor;
        channel.maxPeak = MeterScale::kFloor;
        channel.clipped = false;
    }
    loudnessMeter_.ResetIntegrated();
    reading_.integrated = MeterScale::kFloor;
}

void LevelMeterModel::Restart() {
    KillTimer(window_, kRestartTimer);
    if (!shouldRun_) return;
    capture_.Stop();
    outputDeviceName_ = AudioCapture::DefaultOutputDeviceName();
    if (StartCapture()) {
        if (!timerRunning_) {
            lastTick_ = Now();
            ScheduleTimer();
        }
        return;
    }
    KillTimer(window_, kTickTimer);
    timerRunning_ = false;
    for (Channel& channel : channels_) {
        channel.level = MeterScale::kFloor;
        channel.peakHold = MeterScale::kFloor;
    }
}

// Loopback capture belongs to one device, so it has to move when the default output changes.
void LevelMeterModel::OnDefaultDeviceChanged() {
    outputDeviceName_ = AudioCapture::DefaultOutputDeviceName();
    if (shouldRun_) Restart();
}

// Usually the device went away. Give Windows a moment to pick the new default, then reopen.
void LevelMeterModel::OnCaptureFailed(LPARAM generation) {
    if (!shouldRun_ || generation != generation_) return;
    capture_.Stop();
    SetTimer(window_, kRestartTimer, kRestartDelay, nullptr);
}

bool LevelMeterModel::Tick() {
    const double now = Now();
    const float release = kReleaseRate * float(now - lastTick_);
    lastTick_ = now;

    float peaks[2];
    capture_.DrainPeaks(peaks);
    bool changed = false;
    for (int index = 0; index < 2; ++index) {
        const float peak = MeterScale::DecibelsFromAmplitude(peaks[index]);
        Channel channel = channels_[index];
        channel.level = std::max({peak, channel.level - release, MeterScale::kFloor});
        if (peak > MeterScale::kFloor && peak >= channel.peakHold) {
            channel.peakHold = peak;
            channel.peakHoldTime = now;
        } else if (now - channel.peakHoldTime > kPeakHoldDuration) {
            channel.peakHold = std::max(channel.peakHold - release, channel.level);
        }
        channel.maxPeak = std::max(channel.maxPeak, peak);
        if (peaks[index] >= 1) channel.clipped = true;
        // Report no change when nothing moved so a silent meter doesn't redraw 60 times a second.
        changed = changed || !LooksTheSame(channel, channels_[index]);
        channels_[index] = channel;
    }

    // Loudness only changes when a 100 ms block completes.
    capture_.DrainLoudnessBlocks(blocks_);
    if (blocks_.empty()) return changed;
    for (double blockPower : blocks_) loudnessMeter_.Add(blockPower);
    blocks_.clear();
    const LoudnessReading reading{std::max(loudnessMeter_.ShortTerm(), MeterScale::kFloor),
                                  std::max(loudnessMeter_.Integrated(), MeterScale::kFloor)};
    if (reading.shortTerm != reading_.shortTerm || reading.integrated != reading_.integrated) {
        reading_ = reading;
        changed = true;
    }
    return changed;
}

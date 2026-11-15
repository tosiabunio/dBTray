#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Captures the mix of everything playing on the default output device with WASAPI loopback and
// reports per-channel peak amplitude and loudness, which the UI drains at display rate.
//
// Capture runs on its own thread, which reads the loopback stream every 10 ms and measures each
// packet into a running peak and a LoudnessAnalyzer.
class AudioCapture {
public:
    AudioCapture() = default;
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Starts capturing and waits until the stream runs. If capture fails later (the device is
    // removed, say), `failureMessage` is posted to `window` with the HRESULT as wParam and `tag`
    // as lParam.
    bool Start(HWND window, UINT failureMessage, LPARAM tag, std::wstring& error);
    void Stop();
    bool IsRunning() const { return thread_ != nullptr; }

    // Highest absolute sample value per channel (0 = silence, 1 = 0 dBFS) since the previous call.
    void DrainPeaks(float peaks[2]);
    // Appends the K-weighted power of each 100 ms block completed since the previous call.
    void DrainLoudnessBlocks(std::vector<double>& blocks);

    // Friendly name of the default output device, such as "Speakers (Realtek(R) Audio)".
    static std::wstring DefaultOutputDeviceName();

private:
    static DWORD WINAPI ThreadMain(void* self);
    void Run();
    void Publish(const float peaks[2], std::vector<double>& blocks);
    void CloseHandles();

    HANDLE thread_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE startedEvent_ = nullptr;
    HRESULT startResult_ = S_OK;
    const wchar_t* startStage_ = L"";
    HWND window_ = nullptr;
    UINT failureMessage_ = 0;
    LPARAM tag_ = 0;

    SRWLOCK lock_ = SRWLOCK_INIT;
    float peaks_[2] = {0, 0};
    std::vector<double> blocks_;
};

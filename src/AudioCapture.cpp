#include "AudioCapture.h"

#include "Loudness.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kBufferDuration = 200 * 10000;  // 200 ms in 100 ns units
constexpr DWORD kPollInterval = 10;                       // ms
// Windows delivers no loopback packets while nothing is playing. After this long without one,
// the gap is measured as the silence it is.
constexpr double kSilenceGap = 0.25;  // s

const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
// PKEY_Device_FriendlyName
const PROPERTYKEY kDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

enum class SampleType { Unsupported, Float32, Int16, Int24, Int32 };

struct CaptureStream {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    SampleType type = SampleType::Unsupported;
    UINT channels = 0;
    UINT bytesPerSample = 0;
    UINT blockAlign = 0;
    double sampleRate = 0;
};

bool Describe(const WAVEFORMATEX& format, CaptureStream& stream) {
    bool isFloat = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = format.wFormatTag == WAVE_FORMAT_PCM;
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        isFloat = IsEqualGUID(extensible.SubFormat, kSubtypeFloat) != 0;
        isPcm = IsEqualGUID(extensible.SubFormat, kSubtypePcm) != 0;
    }
    stream.type = SampleType::Unsupported;
    if (isFloat && format.wBitsPerSample == 32) stream.type = SampleType::Float32;
    if (isPcm && format.wBitsPerSample == 16) stream.type = SampleType::Int16;
    if (isPcm && format.wBitsPerSample == 24) stream.type = SampleType::Int24;
    if (isPcm && format.wBitsPerSample == 32) stream.type = SampleType::Int32;
    stream.channels = format.nChannels;
    stream.bytesPerSample = format.wBitsPerSample / 8;
    stream.blockAlign = format.nBlockAlign;
    stream.sampleRate = format.nSamplesPerSec;
    return stream.type != SampleType::Unsupported && stream.channels > 0 &&
           stream.sampleRate > 0 && stream.blockAlign >= stream.channels * stream.bytesPerSample;
}

HRESULT Activate(IMMDevice* device, ComPtr<IAudioClient>& client) {
    client.Reset();
    return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(client.GetAddressOf()));
}

HRESULT OpenLoopback(IMMDevice* device, CaptureStream& stream) {
    ComPtr<IAudioClient> client;
    HRESULT hr = Activate(device, client);
    if (FAILED(hr)) return hr;
    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr)) return hr;

    // Ask the audio engine for a stereo float mixdown at the mix rate, the counterpart of dBar's
    // stereo global tap: surround output is folded down the way Windows does it for playback.
    WAVEFORMATEXTENSIBLE stereo = {};
    stereo.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    stereo.Format.nChannels = 2;
    stereo.Format.nSamplesPerSec = mix->nSamplesPerSec;
    stereo.Format.wBitsPerSample = 32;
    stereo.Format.nBlockAlign = 2 * sizeof(float);
    stereo.Format.nAvgBytesPerSec = stereo.Format.nSamplesPerSec * stereo.Format.nBlockAlign;
    stereo.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    stereo.Samples.wValidBitsPerSample = 32;
    stereo.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    stereo.SubFormat = kSubtypeFloat;
    const DWORD convert = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | convert,
                            kBufferDuration, 0, &stereo.Format, nullptr);
    if (SUCCEEDED(hr)) {
        Describe(stereo.Format, stream);
    } else {
        // The conversion was refused: read the first two channels of the mix format instead.
        hr = Activate(device, client);
        if (SUCCEEDED(hr)) {
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    kBufferDuration, 0, mix, nullptr);
        }
        if (SUCCEEDED(hr) && !Describe(*mix, stream)) hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    CoTaskMemFree(mix);
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&stream.capture));
    if (SUCCEEDED(hr)) stream.client = client;
    return hr;
}

float ReadSample(const BYTE* p, SampleType type) {
    switch (type) {
    case SampleType::Float32: {
        float value;
        std::memcpy(&value, p, sizeof(value));
        return value;
    }
    case SampleType::Int16: {
        int16_t value;
        std::memcpy(&value, p, sizeof(value));
        return float(value) / 32768.0f;
    }
    case SampleType::Int24: {
        const uint32_t bits = uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24;
        return float(int32_t(bits)) / 2147483648.0f;
    }
    case SampleType::Int32: {
        int32_t value;
        std::memcpy(&value, p, sizeof(value));
        return float(value) / 2147483648.0f;
    }
    default:
        return 0;
    }
}

// Measures the first two channels of a packet (a mono stream is read as both channels).
void Measure(const CaptureStream& stream, const BYTE* data, UINT32 frames,
             LoudnessAnalyzer& analyzer, float peaks[2], std::vector<double>& blocks,
             std::vector<float>& scratch) {
    const float* left;
    const float* right;
    size_t stride;
    if (stream.type == SampleType::Float32) {
        const auto* samples = reinterpret_cast<const float*>(data);
        left = samples;
        right = stream.channels > 1 ? samples + 1 : samples;
        stride = stream.blockAlign / sizeof(float);
    } else {
        scratch.resize(size_t(frames) * 2);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const BYTE* p = data + size_t(frame) * stream.blockAlign;
            const float l = ReadSample(p, stream.type);
            scratch[2 * size_t(frame)] = l;
            scratch[2 * size_t(frame) + 1] =
                stream.channels > 1 ? ReadSample(p + stream.bytesPerSample, stream.type) : l;
        }
        left = scratch.data();
        right = left + 1;
        stride = 2;
    }

    float leftPeak = peaks[0];
    float rightPeak = peaks[1];
    for (size_t frame = 0; frame < frames; ++frame) {
        leftPeak = std::max(leftPeak, std::fabs(left[frame * stride]));
        rightPeak = std::max(rightPeak, std::fabs(right[frame * stride]));
    }
    peaks[0] = leftPeak;
    peaks[1] = rightPeak;
    analyzer.Process(left, stride, right, stride, frames, blocks);
}

std::wstring DescribeFailure(const wchar_t* stage, HRESULT hr) {
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return L"No audio output device is available.";
    if (hr == AUDCLNT_E_SERVICE_NOT_RUNNING) return L"The Windows Audio service isn't running.";
    wchar_t text[128];
    swprintf_s(text, L"%s failed (0x%08lX).", stage, static_cast<unsigned long>(hr));
    return text;
}

}  // namespace

AudioCapture::~AudioCapture() {
    Stop();
}

bool AudioCapture::Start(HWND window, UINT failureMessage, LPARAM tag, std::wstring& error) {
    if (thread_) return true;
    window_ = window;
    failureMessage_ = failureMessage;
    tag_ = tag;
    startResult_ = E_FAIL;
    startStage_ = L"Starting audio capture";
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    startedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ && startedEvent_) thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    if (!thread_) {
        CloseHandles();
        error = L"Starting the capture thread failed.";
        return false;
    }

    const HANDLE waits[] = {startedEvent_, thread_};
    WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (WaitForSingleObject(startedEvent_, 0) == WAIT_OBJECT_0 && SUCCEEDED(startResult_)) return true;
    error = DescribeFailure(startStage_, startResult_);
    Stop();
    return false;
}

void AudioCapture::Stop() {
    if (thread_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    CloseHandles();
    AcquireSRWLockExclusive(&lock_);
    peaks_[0] = peaks_[1] = 0;
    blocks_.clear();
    ReleaseSRWLockExclusive(&lock_);
}

void AudioCapture::CloseHandles() {
    if (stopEvent_) CloseHandle(stopEvent_);
    if (startedEvent_) CloseHandle(startedEvent_);
    stopEvent_ = startedEvent_ = nullptr;
}

void AudioCapture::DrainPeaks(float peaks[2]) {
    AcquireSRWLockExclusive(&lock_);
    peaks[0] = peaks_[0];
    peaks[1] = peaks_[1];
    peaks_[0] = peaks_[1] = 0;
    ReleaseSRWLockExclusive(&lock_);
}

void AudioCapture::DrainLoudnessBlocks(std::vector<double>& blocks) {
    AcquireSRWLockExclusive(&lock_);
    blocks.insert(blocks.end(), blocks_.begin(), blocks_.end());
    blocks_.clear();
    ReleaseSRWLockExclusive(&lock_);
}

// Collects the running per-channel maximum between UI refreshes so no transient is missed.
void AudioCapture::Publish(const float peaks[2], std::vector<double>& blocks) {
    AcquireSRWLockExclusive(&lock_);
    peaks_[0] = std::max(peaks_[0], peaks[0]);
    peaks_[1] = std::max(peaks_[1], peaks[1]);
    blocks_.insert(blocks_.end(), blocks.begin(), blocks.end());
    ReleaseSRWLockExclusive(&lock_);
    blocks.clear();
}

DWORD WINAPI AudioCapture::ThreadMain(void* self) {
    static_cast<AudioCapture*>(self)->Run();
    return 0;
}

void AudioCapture::Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = com;
    const wchar_t* stage = L"Finding the output device";
    CaptureStream stream;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(hr)) {
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
        }
        if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (SUCCEEDED(hr)) {
            stage = L"Opening the output device";
            hr = OpenLoopback(device.Get(), stream);
        }
        if (SUCCEEDED(hr)) {
            stage = L"Starting audio capture";
            hr = stream.client->Start();
        }
    }
    startResult_ = hr;
    startStage_ = stage;
    SetEvent(startedEvent_);

    if (SUCCEEDED(hr)) {
        LoudnessAnalyzer analyzer(stream.sampleRate);
        std::vector<double> blocks;
        std::vector<float> scratch;
        blocks.reserve(16);
        LARGE_INTEGER frequency, now;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);
        LONGLONG lastPacket = now.QuadPart;
        UINT64 silentFrames = 0;

        while (WaitForSingleObject(stopEvent_, kPollInterval) == WAIT_TIMEOUT) {
            float peaks[2] = {0, 0};
            bool received = false;
            UINT32 packetFrames = 0;
            while (SUCCEEDED(hr = stream.capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = stream.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr) || hr == AUDCLNT_S_BUFFER_EMPTY) break;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    analyzer.ProcessSilence(frames, blocks);
                } else {
                    Measure(stream, data, frames, analyzer, peaks, blocks, scratch);
                }
                hr = stream.capture->ReleaseBuffer(frames);
                if (FAILED(hr)) break;
                received = true;
            }
            if (FAILED(hr)) break;

            QueryPerformanceCounter(&now);
            if (received) {
                lastPacket = now.QuadPart;
                silentFrames = 0;
            } else {
                // Nothing is playing, so nothing arrives. Loudness still has to advance through
                // the silence (short-term loudness falls back as its 3 s window empties), so feed
                // the analyzer the frames that would have been delivered.
                const double idle = double(now.QuadPart - lastPacket) / double(frequency.QuadPart);
                if (idle >= kSilenceGap) {
                    const UINT64 due = UINT64(idle * stream.sampleRate);
                    if (due > silentFrames) {
                        analyzer.ProcessSilence(size_t(due - silentFrames), blocks);
                        silentFrames = due;
                    }
                }
            }
            Publish(peaks, blocks);
        }

        stream.client->Stop();
        if (FAILED(hr)) PostMessageW(window_, failureMessage_, static_cast<WPARAM>(hr), tag_);
    }

    stream.capture.Reset();
    stream.client.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
}

std::wstring AudioCapture::DefaultOutputDeviceName() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IPropertyStore> properties;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
        return {};
    }
    std::wstring name;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(properties->GetValue(kDeviceFriendlyName, &value)) && value.vt == VT_LPWSTR) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

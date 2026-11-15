#pragma once

#include <cstddef>
#include <deque>
#include <limits>
#include <vector>

// Loudness measurement as defined by ITU-R BS.1770 (the basis of EBU R128 and of the
// normalization streaming services apply). LoudnessAnalyzer K-weights the audio on the capture
// thread and emits 100 ms blocks of power; LoudnessMeter turns those into readings.
namespace Loudness {

// Integrated loudness most streaming services normalize to.
constexpr float kTarget = -14.0f;

// Loudness in LUFS of a mean-square power summed over channels.
float LufsFromPower(double power);

}  // namespace Loudness

// Second-order IIR section in transposed direct form II.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    double Process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// BS.1770's two-stage K-weighting: a high shelf modelling the head's acoustic effect, then a
// high-pass. Coefficients are derived for the actual sample rate; at 48 kHz they match the values
// tabulated in the standard.
class KWeightingFilter {
public:
    explicit KWeightingFilter(double sampleRate);
    double Process(double sample) { return highPass_.Process(shelf_.Process(sample)); }
    // Long silence would otherwise decay the filter state into slow denormal arithmetic.
    void FlushDenormals();

private:
    Biquad shelf_;
    Biquad highPass_;
};

// Runs on the capture thread: K-weights both channels and emits the mean-square power of every
// 100 ms block, the step BS.1770's 400 ms windows and gating advance by.
class LoudnessAnalyzer {
public:
    explicit LoudnessAnalyzer(double sampleRate);

    // Strides let the same code read interleaved and planar buffers. The power of each completed
    // block is appended to `completedBlocks`.
    void Process(const float* left, size_t leftStride, const float* right, size_t rightStride,
                 size_t frameCount, std::vector<double>& completedBlocks);
    void ProcessSilence(size_t frameCount, std::vector<double>& completedBlocks);

private:
    void Accumulate(double energy, std::vector<double>& completedBlocks);

    KWeightingFilter leftFilter_;
    KWeightingFilter rightFilter_;
    size_t framesPerBlock_;
    double blockEnergy_ = 0;
    size_t blockFrames_ = 0;
};

// Short-term and gated integrated loudness, fed with LoudnessAnalyzer's 100 ms blocks.
class LoudnessMeter {
public:
    void Add(double blockPower);
    void ResetIntegrated();

    // Loudness of the last 3 s in LUFS (of what's available until 3 s have passed).
    float ShortTerm() const { return shortTerm_; }
    // Gated loudness since the meter started or ResetIntegrated() was called, in LUFS.
    float Integrated() const { return integrated_; }

private:
    void UpdateIntegrated();

    std::deque<double> recentBlocks_;
    // Power of every 400 ms window (75% overlap) above the absolute gate since the last reset.
    std::vector<double> gatingWindows_;
    float shortTerm_ = -std::numeric_limits<float>::infinity();
    float integrated_ = -std::numeric_limits<float>::infinity();
};

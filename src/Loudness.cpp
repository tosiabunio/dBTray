#include "Loudness.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kBlocksPerMomentaryWindow = 4;   // 400 ms
constexpr size_t kBlocksPerShortTermWindow = 30;  // 3 s
const double kAbsoluteGatePower = std::pow(10.0, (-70 + 0.691) / 10);  // -70 LUFS

template <typename Iterator>
double Mean(Iterator begin, Iterator end) {
    double sum = 0;
    size_t count = 0;
    for (; begin != end; ++begin, ++count) sum += *begin;
    return count ? sum / double(count) : 0;
}

void Flush(double& value) {
    if (std::fabs(value) < 1e-25) value = 0;
}

}  // namespace

float Loudness::LufsFromPower(double power) {
    return power > 0 ? float(-0.691 + 10 * std::log10(power))
                     : -std::numeric_limits<float>::infinity();
}

KWeightingFilter::KWeightingFilter(double sampleRate) {
    const double shelfFrequency = 1681.974450955533;
    const double shelfGain = 3.999843853973347;
    const double shelfQ = 0.7071752369554196;
    double k = std::tan(kPi * shelfFrequency / sampleRate);
    const double vh = std::pow(10.0, shelfGain / 20);
    const double vb = std::pow(vh, 0.4996667741545416);
    double a0 = 1 + k / shelfQ + k * k;
    shelf_.b0 = (vh + vb * k / shelfQ + k * k) / a0;
    shelf_.b1 = 2 * (k * k - vh) / a0;
    shelf_.b2 = (vh - vb * k / shelfQ + k * k) / a0;
    shelf_.a1 = 2 * (k * k - 1) / a0;
    shelf_.a2 = (1 - k / shelfQ + k * k) / a0;

    const double highPassFrequency = 38.13547087602444;
    const double highPassQ = 0.5003270373238773;
    k = std::tan(kPi * highPassFrequency / sampleRate);
    a0 = 1 + k / highPassQ + k * k;
    highPass_.b0 = 1;
    highPass_.b1 = -2;
    highPass_.b2 = 1;
    highPass_.a1 = 2 * (k * k - 1) / a0;
    highPass_.a2 = (1 - k / highPassQ + k * k) / a0;
}

void KWeightingFilter::FlushDenormals() {
    Flush(shelf_.z1);
    Flush(shelf_.z2);
    Flush(highPass_.z1);
    Flush(highPass_.z2);
}

LoudnessAnalyzer::LoudnessAnalyzer(double sampleRate)
    : leftFilter_(sampleRate),
      rightFilter_(sampleRate),
      framesPerBlock_(std::max<size_t>(1, size_t(std::lround(sampleRate / 10)))) {}

void LoudnessAnalyzer::Process(const float* left, size_t leftStride, const float* right,
                               size_t rightStride, size_t frameCount,
                               std::vector<double>& completedBlocks) {
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const double l = leftFilter_.Process(left[frame * leftStride]);
        const double r = rightFilter_.Process(right[frame * rightStride]);
        Accumulate(l * l + r * r, completedBlocks);
    }
    leftFilter_.FlushDenormals();
    rightFilter_.FlushDenormals();
}

void LoudnessAnalyzer::ProcessSilence(size_t frameCount, std::vector<double>& completedBlocks) {
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const double l = leftFilter_.Process(0);
        const double r = rightFilter_.Process(0);
        Accumulate(l * l + r * r, completedBlocks);
    }
    leftFilter_.FlushDenormals();
    rightFilter_.FlushDenormals();
}

void LoudnessAnalyzer::Accumulate(double energy, std::vector<double>& completedBlocks) {
    blockEnergy_ += energy;
    if (++blockFrames_ == framesPerBlock_) {
        completedBlocks.push_back(blockEnergy_ / double(framesPerBlock_));
        blockEnergy_ = 0;
        blockFrames_ = 0;
    }
}

void LoudnessMeter::Add(double blockPower) {
    recentBlocks_.push_back(blockPower);
    if (recentBlocks_.size() > kBlocksPerShortTermWindow) recentBlocks_.pop_front();
    shortTerm_ = Loudness::LufsFromPower(Mean(recentBlocks_.begin(), recentBlocks_.end()));

    if (recentBlocks_.size() < kBlocksPerMomentaryWindow) return;
    const double windowPower =
        Mean(recentBlocks_.end() - kBlocksPerMomentaryWindow, recentBlocks_.end());
    if (!(windowPower > kAbsoluteGatePower)) return;
    gatingWindows_.push_back(windowPower);
    UpdateIntegrated();
}

void LoudnessMeter::ResetIntegrated() {
    gatingWindows_.clear();
    integrated_ = -std::numeric_limits<float>::infinity();
}

// BS.1770 gating: ignore windows more than 10 LU below the average of those above -70 LUFS.
void LoudnessMeter::UpdateIntegrated() {
    const double relativeGatePower = Mean(gatingWindows_.begin(), gatingWindows_.end()) / 10;
    double sum = 0;
    size_t count = 0;
    for (double power : gatingWindows_) {
        if (power > relativeGatePower) {
            sum += power;
            ++count;
        }
    }
    integrated_ = Loudness::LufsFromPower(count ? sum / double(count) : 0);
}

// Checks the loudness measurement against the EBU Tech 3341 reference cases that use plain
// stereo sine waves, and the capture thread's handling of silence.
//
//   build.cmd test

#include "../src/Loudness.h"
#include "../src/MeterScale.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTolerance = 0.1;  // LU, as Tech 3341 allows

struct Segment {
    double dbfs;          // level of a 1 kHz stereo sine; 0 dBFS is full scale
    double seconds;
    bool silent = false;  // fed the way the capture thread feeds silence Windows didn't deliver
};

struct Result {
    float integrated;
    float shortTermLow;   // range of short-term loudness once 3 s have passed
    float shortTermHigh;
    float shortTermLast;
};

Result Measure(double sampleRate, std::initializer_list<Segment> segments) {
    LoudnessAnalyzer analyzer(sampleRate);
    LoudnessMeter meter;
    std::vector<double> blocks;
    std::vector<float> buffer(2 * 1000);
    Result result{0, 1e9f, -1e9f, 0};
    size_t blockCount = 0;
    double phase = 0;
    const double step = 2 * kPi * 1000 / sampleRate;

    for (const Segment& segment : segments) {
        const float amplitude = float(std::pow(10.0, segment.dbfs / 20));
        const size_t total = size_t(std::llround(segment.seconds * sampleRate));
        for (size_t done = 0; done < total;) {
            const size_t frames = std::min<size_t>(buffer.size() / 2, total - done);
            if (segment.silent) {
                analyzer.ProcessSilence(frames, blocks);
            } else {
                for (size_t frame = 0; frame < frames; ++frame) {
                    buffer[2 * frame] = buffer[2 * frame + 1] = amplitude * float(std::sin(phase));
                    phase += step;
                }
                analyzer.Process(buffer.data(), 2, buffer.data() + 1, 2, frames, blocks);
            }
            for (double power : blocks) {
                meter.Add(power);
                if (++blockCount > 30) {
                    result.shortTermLow = std::min(result.shortTermLow, meter.ShortTerm());
                    result.shortTermHigh = std::max(result.shortTermHigh, meter.ShortTerm());
                }
            }
            blocks.clear();
            done += frames;
        }
    }
    result.integrated = meter.Integrated();
    result.shortTermLast = meter.ShortTerm();
    return result;
}

int failures = 0;

void Expect(const char* name, double sampleRate, double actual, double expected) {
    const bool passed = std::fabs(actual - expected) <= kTolerance;
    if (!passed) ++failures;
    std::printf("%s  %-34s %6.0f Hz  %7.2f  (expected %.1f)\n", passed ? "ok  " : "FAIL", name,
                sampleRate, actual, expected);
}

void ExpectTrue(const char* name, bool condition) {
    if (!condition) ++failures;
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", name);
}

}  // namespace

int main() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        Expect("3341 case 1: integrated", rate, Measure(rate, {{-23, 20}}).integrated, -23);
        Expect("3341 case 2: integrated", rate, Measure(rate, {{-33, 20}}).integrated, -33);
        Expect("3341 case 3: integrated", rate,
               Measure(rate, {{-36, 10}, {-23, 60}, {-36, 10}}).integrated, -23);
        Expect("3341 case 4: integrated", rate,
               Measure(rate, {{-72, 10}, {-36, 10}, {-23, 60}, {-36, 10}, {-72, 10}}).integrated, -23);
        Expect("3341 case 5: integrated", rate,
               Measure(rate, {{-26, 20}, {-20, 20.1}, {-26, 20}}).integrated, -23);

        // Case 9: every 3 s window holds 1.34 s at -20 dBFS and 1.66 s at -30 dBFS.
        const Result shortTerm = Measure(rate, {{-20, 1.34}, {-30, 1.66}, {-20, 1.34}, {-30, 1.66},
                                                {-20, 1.34}, {-30, 1.66}, {-20, 1.34}, {-30, 1.66},
                                                {-20, 1.34}, {-30, 1.66}});
        Expect("3341 case 9: short-term minimum", rate, shortTerm.shortTermLow, -23);
        Expect("3341 case 9: short-term maximum", rate, shortTerm.shortTermHigh, -23);

        // Silence that Windows doesn't deliver must still empty the short-term window, and must
        // not drag integrated loudness down (it is below the gates).
        const Result paused = Measure(rate, {{-23, 20}, {0, 5, true}});
        Expect("silence keeps integrated", rate, paused.integrated, -23);
        ExpectTrue("silence empties short-term", paused.shortTermLast < -100);
    }

    ExpectTrue("scale: floor sits at 0", MeterScale::Position(MeterScale::kFloor) == 0);
    ExpectTrue("scale: 0 dBFS sits at 1", MeterScale::Position(0) == 1);
    ExpectTrue("scale: silence reads as the floor", MeterScale::DecibelsFromAmplitude(0) == MeterScale::kFloor);
    ExpectTrue("scale: full scale reads as 0 dB", MeterScale::DecibelsFromAmplitude(1) == 0);
    ExpectTrue("scale: zones", MeterScale::ZoneAt(MeterScale::Position(-13)) == MeterScale::Zone::Green &&
                                   MeterScale::ZoneAt(MeterScale::Position(-6)) == MeterScale::Zone::Yellow &&
                                   MeterScale::ZoneAt(MeterScale::Position(-2)) == MeterScale::Zone::Red);

    std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}

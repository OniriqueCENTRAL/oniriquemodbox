// Onirique ModBox — flanger, BBD chorus and dimension in one line (no dependency)
#pragma once

#include <cstdint>
#include <vector>

namespace mdx
{

constexpr int kMaxVoices = 8;
constexpr int kMaxChannels = 2;
constexpr float kMinDelayMs = 0.3f;
constexpr float kMaxDelayMs = 50.0f;
constexpr int kNumStageChoices = 5;     // 256, 512, 1024, 2048, 4096

/** How many stages the bucket brigade of a choice has. */
constexpr int stagesOf (int choice) noexcept { return 256 << choice; }

enum Shape { sine = 0, triangle = 1 };

struct Settings
{
    // Circuit: from a perfect delay line to a bucket brigade. Everything
    // else in this family counts for nothing while circuit is 0.
    float circuit = 0.0f;       // 0 digital .. 1 analog
    float drive = 0.3f;         // 0..1, into the line and round the feedback loop
    float drift = 0.3f;         // 0..1, how much the clock wanders
    float noise = 0.0f;         // 0..1, hiss
    int stages = 2;             // a choice: see stagesOf()

    // Density.
    int voices = 1;             // 1..kMaxVoices taps on the line
    float spread = 0.5f;        // 0..1, how far apart the voices' delays are
    float keepHz = 20.0f;       // below this the sound is left alone; 20 = off

    // Modulation.
    float rateHz = 0.35f;
    float depth = 0.5f;         // 0..1
    int shape = sine;
    float rate2Hz = 0.11f;      // the second LFO
    float lfo2 = 0.0f;          // 0..1, how much of the sweep is the second LFO's
    float stereoDeg = 90.0f;    // 0..180, the right LFO ahead of the left

    // Intensity.
    float delayMs = 4.0f;       // the middle of the sweep
    float feedback = 0.35f;     // -1..1
    float cross = 0.0f;         // 0..1, how much of the feedback changes side
    float mix = 0.5f;           // 0 dry .. 1 wet

    // Texture: what is done to the voices once they have been read, each
    // of the three out of the way at 0.
    float crush = 0.0f;         // 0..1, fewer bits: see crushBits()
    float downsample = 0.0f;    // 0..1, a lower sample rate: see holdHz()
    float distortion = 0.0f;    // 0..1

    float outDb = 0.0f;
    bool autoGain = false;      // bring the output back to the level of the input
    bool bypass = false;
};

/** The feedback as it is applied: the knob's 100 % stops short of a loop that
    never dies, and shorter still where the circuit's filter has a peak. */
float feedbackGain (const Settings&) noexcept;

/** Where a bucket brigade of so many stages has to be filtered for a delay:
    a third of its clock, which falls as the delay grows. */
double bbdCutoffHz (int stages, double delaySeconds, double sampleRate) noexcept;

/** What the drive adds to the wet level, as an amplitude. */
float driveMakeup (const Settings&) noexcept;

/** What the feedback takes off the wet level, as an amplitude: a loop that
    rings is louder, and half of that, in dB, is given back. */
float feedbackMakeup (const Settings&) noexcept;

/** How many bits the crusher leaves: sixteen where it starts, three at its most. */
float crushBits (float crush) noexcept;

/** The rate the voices are held at when they are downsampled: 40 kHz where
    it starts, 300 Hz at its most. */
float holdHz (float downsample) noexcept;

/** How far the auto gain may go either way, as an amplitude: 12 dB. */
constexpr float kAutoGainRange = 4.0f;

/** The Q of the circuit's low-pass. */
constexpr double kBbdFilterQ = 0.9;

/** Where a voice sits when nothing is sweeping it: the middle delay, moved
    by its share of the spread. */
float restDelayMs (const Settings&, int voice) noexcept;

/** Up to eight taps on one modulated delay line per channel.

    Digital, the line is read with a windowed sinc and nothing else is done
    to it: no noise, a flat response, a feedback that is only a gain.

    Analog, it behaves as a bucket brigade does: what is written into it is
    soft-clipped, feedback included; what is read from it goes through a
    resonant low-pass that closes as the delay grows, since a longer delay
    is a slower clock; the clock itself wanders, slowly and quickly; and
    there is hiss if asked for.

    Either way the voices can then be roughened, outside the loop: held at
    a lower sample rate, cut down to a few bits, and distorted.

    The bass can be taken out of all of it: below the keep frequency the
    input goes straight to the output.
*/
class Modulator
{
public:
    /** Allocates. Everything after this is free of allocation. */
    void prepare (double sampleRate, int numInputChannels, int numOutputChannels);
    void reset() noexcept;

    void setSettings (const Settings& s) noexcept { settings = s; }

    /** in and out may be the same buffers. */
    void process (const float* const* in, float* const* out, int numSamples) noexcept;

    /** What the auto gain is applying just now, as an amplitude: 1 while it is off. */
    float autoGainNow() const noexcept { return 1.0f + autoBlend * (autoGain - 1.0f); }

    /** Where a voice is reading the line just now, for the editor. */
    float voiceDelayMs (int voice, int channel) const noexcept;
    float voiceGain (int voice) const noexcept { return voices[voice].gain.target; }

private:
    struct Ramp
    {
        void aim (float to, float perSample, bool snap) noexcept
        {
            now = snap ? to : target;
            target = to;
            step = (target - now) * perSample;
        }

        float next() noexcept { now += step; return now; }

        float now = 0.0f, target = 0.0f, step = 0.0f;
    };

    /** A two-pole filter whose low and high outputs are taken together. */
    struct Svf
    {
        void set (double hz, double q, double sampleRate) noexcept;
        void clear() noexcept { ic1 = ic2 = 0.0f; }

        float low (float v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }

        float high (float v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v0 - k * v1 - v2;
        }

        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4142135f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    struct Voice
    {
        Ramp gain;
        float gainNow = 0.0f;                       // smoothed, at the control rate
        double delay[kMaxChannels] = {};            // samples, moving every sample
        double delayStep[kMaxChannels] = {};
        double delayAim[kMaxChannels] = {};
        Svf filter[kMaxChannels];
        float wow = 0.0f, flutter = 0.0f;
        bool idle = true;
    };

    void tick() noexcept;
    float read (const std::vector<float>& line, double delay) const noexcept;
    float random() noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int numIn = 2, numOut = 2;

    std::vector<float> lines[kMaxChannels];
    int mask = 0, writeAt = 0;
    double minDelay = 8.0, maxDelay = 4800.0;

    // The control rate: what moves slowly is worked out once every so many
    // samples and ramped to in between.
    int control = 16, countdown = 0;
    float perSample = 1.0f / 16.0f;
    bool snap = true;

    Voice voices[kMaxVoices];
    double phase1 = 0.0, phase2 = 0.0;
    uint32_t seed = 0x2545f491u;

    // The settings as they are heard, each gliding to where it was put.
    struct Glided
    {
        float circuit = 0.0f, drive = 0.0f, drift = 0.0f, noise = 0.0f;
        float spread = 0.0f, keepHz = 20.0f, keep = 0.0f;
        float depth = 0.0f, lfo2 = 0.0f, stereo = 0.0f;
        float delayMs = 4.0f, feedback = 0.0f, cross = 0.0f, mix = 0.0f;
        float out = 1.0f, bypass = 0.0f, autoGain = 0.0f;
        float crush = 0.0f, downsample = 0.0f, distortion = 0.0f;
    } glided;

    // The texture: the step of the crusher, how much of a held sample goes
    // by with each real one, and the distortion's gain in and out.
    float crushStep = 0.0f, holdStep = 1.0f, holdPhase = 0.0f, held[kMaxChannels] = {};
    float distortionGain = 1.0f, distortionLevel = 1.0f, distortionBlend = 0.0f;

    Ramp dryGain, wetGain, feedbackNow, outGain, bypassNow, keepNow, autoNow;

    // The auto gain: the power going in and the power coming out, each followed
    // over a few tenths of a second, and the gain that makes the second the first.
    double powerIn = 0.0, powerOut = 0.0;
    float autoGain = 1.0f, autoBlend = 0.0f, powerCoef = 0.0f, autoCoef = 0.0f;
    float analog = 0.0f, crossNow = 0.0f, driveNow = 1.0f, noiseNow = 0.0f;
    bool analogRunning = false;

    // The crossover: two two-pole filters in a row each way.
    Svf keepLow[kMaxChannels][2], keepHigh[kMaxChannels][2];
    bool keepRunning = false;

    float slowCoef = 0.02f, delayCoef = 0.005f, voiceCoef = 0.1f, gainCoef = 0.02f;
    float wowCoef = 0.0f, flutterCoef = 0.0f, wowScale = 1.0f, flutterScale = 1.0f;
};

} // namespace mdx

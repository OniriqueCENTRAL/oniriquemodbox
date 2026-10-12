#include "Modulator.h"

#include <algorithm>
#include <cmath>

namespace mdx
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // The line is read through sixteen points of a windowed sinc, taken from
    // a table of them at 256 places between two samples.
    constexpr int kTaps = 16;
    constexpr int kHalf = kTaps / 2;
    constexpr int kPhases = 256;

    constexpr float kFeedbackMax = 0.96f;
    constexpr float kFilterPeak = 0.082f;   // what a filter of that Q adds at its peak
    constexpr float kDriveDb = 18.0f;

    double bessel0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;

        for (int i = 1; i < 32; ++i)
        {
            term *= (x * 0.5 / i) * (x * 0.5 / i);
            sum += term;
        }

        return sum;
    }

    struct SincTable
    {
        SincTable()
        {
            const double beta = 8.0;

            for (int p = 0; p <= kPhases; ++p)
            {
                const auto frac = (double) p / kPhases;
                double row[kTaps], sum = 0.0;

                for (int j = 0; j < kTaps; ++j)
                {
                    const auto t = (double) (j - (kHalf - 1)) - frac;
                    const auto u = std::min (1.0, std::abs (t) / kHalf);
                    const auto window = bessel0 (beta * std::sqrt (1.0 - u * u)) / bessel0 (beta);
                    const auto sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);

                    row[j] = sinc * window;
                    sum += row[j];
                }

                // Each row passes a steady level as it is.
                for (int j = 0; j < kTaps; ++j)
                    h[p][j] = (float) (row[j] / sum);
            }
        }

        float h[kPhases + 1][kTaps];
    };

    const SincTable& sincTable()
    {
        static const SincTable table;
        return table;
    }

    float clamp01 (float v) noexcept { return std::min (1.0f, std::max (0.0f, v)); }

    float lfo (int shape, double phase) noexcept
    {
        const auto p = phase - std::floor (phase);

        if (shape == triangle)
        {
            const auto q = p + 0.25 - std::floor (p + 0.25);
            return (float) (1.0 - 4.0 * std::abs (q - 0.5));
        }

        return (float) std::sin (2.0 * kPi * p);
    }

    float driveOf (float drive) noexcept
    {
        return std::pow (10.0f, clamp01 (drive) * kDriveDb / 20.0f);
    }

    // How far each voice sits from the middle delay when they are spread.
    const float kStagger[kMaxVoices] = { 0.0f, 1.0f, -0.6f, 0.45f, -0.85f, 0.7f, -0.3f, 0.2f };
}

float feedbackGain (const Settings& s) noexcept
{
    const auto feedback = std::min (1.0f, std::max (-1.0f, s.feedback));
    return feedback * kFeedbackMax / (1.0f + kFilterPeak * clamp01 (s.circuit));
}

double bbdCutoffHz (int stages, double delaySeconds, double sampleRate) noexcept
{
    const auto clock = (double) stages / (2.0 * std::max (1.0e-5, delaySeconds));
    return std::min (std::min (20000.0, 0.45 * sampleRate), std::max (600.0, clock / 3.0));
}

float crushBits (float crush) noexcept
{
    return 16.0f - 13.0f * clamp01 (crush);
}

float holdHz (float downsample) noexcept
{
    return 40000.0f * std::pow (300.0f / 40000.0f, clamp01 (downsample));
}

float feedbackMakeup (const Settings& s) noexcept
{
    const auto gain = feedbackGain (s);
    return std::sqrt (std::sqrt (1.0f - gain * gain));
}

float restDelayMs (const Settings& s, int voice) noexcept
{
    const auto middle = std::min (kMaxDelayMs, std::max (kMinDelayMs, s.delayMs));
    return middle * (1.0f + 0.5f * clamp01 (s.spread) * kStagger[std::min (kMaxVoices - 1, std::max (0, voice))]);
}

float driveMakeup (const Settings& s) noexcept
{
    return 1.0f + clamp01 (s.circuit) * (std::pow (driveOf (s.drive), 0.35f) - 1.0f);
}

//==============================================================================
void Modulator::Svf::set (double hz, double q, double rate) noexcept
{
    const auto g = std::tan (kPi * std::min (hz, 0.49 * rate) / rate);
    const auto damping = 1.0 / q;
    const auto first = 1.0 / (1.0 + g * (g + damping));

    k = (float) damping;
    a1 = (float) first;
    a2 = (float) (g * first);
    a3 = (float) (g * g * first);
}

void Modulator::prepare (double rate, int numInputChannels, int numOutputChannels)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    numIn = std::min ((int) kMaxChannels, std::max (1, numInputChannels));
    numOut = std::min ((int) kMaxChannels, std::max (1, numOutputChannels));

    control = std::max (8, (int) std::lround (16.0 * sampleRate / 48000.0));
    perSample = 1.0f / (float) control;

    // A voice at the far end of the spread, at the top of the sweep.
    minDelay = (double) kHalf;
    maxDelay = 2.0 * kMaxDelayMs * 0.001 * sampleRate;

    int size = 1;

    while (size < (int) maxDelay + 2 * kTaps)
        size <<= 1;

    for (auto& line : lines)
        line.assign ((size_t) size, 0.0f);

    mask = size - 1;

    const auto step = (double) control / sampleRate;
    auto coefficient = [step] (double seconds) { return (float) (1.0 - std::exp (-step / seconds)); };

    slowCoef = coefficient (0.02);
    delayCoef = coefficient (0.06);
    voiceCoef = coefficient (0.003);
    gainCoef = coefficient (0.015);

    // The wander of the clock: noise held to under a hertz, and to a few.
    wowCoef = coefficient (1.0 / (2.0 * kPi * 0.7));
    flutterCoef = coefficient (1.0 / (2.0 * kPi * 7.0));
    wowScale = std::sqrt (3.0f * (2.0f - wowCoef) / wowCoef);
    flutterScale = std::sqrt (3.0f * (2.0f - flutterCoef) / flutterCoef);

    sincTable();
    reset();
}

void Modulator::reset() noexcept
{
    for (auto& line : lines)
        std::fill (line.begin(), line.end(), 0.0f);

    for (auto& voice : voices)
    {
        voice = Voice();
    }

    for (auto& channel : keepLow)
        for (auto& filter : channel)
            filter.clear();

    for (auto& channel : keepHigh)
        for (auto& filter : channel)
            filter.clear();

    holdPhase = 0.0f;
    held[0] = held[1] = 0.0f;

    writeAt = 0;
    countdown = 0;
    phase1 = phase2 = 0.0;
    seed = 0x2545f491u;
    analogRunning = keepRunning = false;
    snap = true;
}

float Modulator::random() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return (float) (int32_t) seed / 2147483648.0f;
}

float Modulator::voiceDelayMs (int voice, int channel) const noexcept
{
    return (float) (voices[voice].delayAim[std::min (channel, numOut - 1)] * 1000.0 / sampleRate);
}

//==============================================================================
void Modulator::tick() noexcept
{
    const auto& s = settings;
    const auto now = snap;
    auto& g = glided;

    auto to = [now] (float& value, float target, float coefficient)
    {
        value = now ? target : value + coefficient * (target - value);
    };

    to (g.circuit, clamp01 (s.circuit), slowCoef);
    to (g.drive, clamp01 (s.drive), slowCoef);
    to (g.drift, clamp01 (s.drift), slowCoef);
    to (g.noise, clamp01 (s.noise), slowCoef);
    to (g.spread, clamp01 (s.spread), slowCoef);
    to (g.depth, clamp01 (s.depth), slowCoef);
    to (g.lfo2, clamp01 (s.lfo2), slowCoef);
    to (g.stereo, std::min (180.0f, std::max (0.0f, s.stereoDeg)), slowCoef);
    to (g.delayMs, std::min (kMaxDelayMs, std::max (kMinDelayMs, s.delayMs)), delayCoef);
    to (g.feedback, std::min (1.0f, std::max (-1.0f, s.feedback)), slowCoef);
    to (g.cross, clamp01 (s.cross), slowCoef);
    to (g.mix, clamp01 (s.mix), slowCoef);
    to (g.out, std::pow (10.0f, s.outDb / 20.0f), slowCoef);
    to (g.bypass, s.bypass ? 1.0f : 0.0f, gainCoef);

    // Digital is exactly digital: what is left of the circuit as it fades is
    // dropped once it is too little to hear.
    analog = (s.circuit <= 0.0f && g.circuit < 1.0e-4f) ? 0.0f : g.circuit;

    if (analog == 0.0f && analogRunning)
        for (auto& voice : voices)
            for (auto& filter : voice.filter)
                filter.clear();

    analogRunning = analog > 0.0f;

    // The crossover.
    const auto keeping = s.keepHz > 20.5f;

    if (keeping && g.keep <= 0.0f)
        g.keepHz = s.keepHz;

    to (g.keepHz, std::max (20.0f, s.keepHz), slowCoef);
    to (g.keep, keeping ? 1.0f : 0.0f, slowCoef);

    if (! keeping && g.keep < 1.0e-4f)
        g.keep = 0.0f;

    if (g.keep > 0.0f)
    {
        for (int c = 0; c < numOut; ++c)
            for (int i = 0; i < 2; ++i)
            {
                keepLow[c][i].set (g.keepHz, 0.70710678, sampleRate);
                keepHigh[c][i].set (g.keepHz, 0.70710678, sampleRate);
            }

        keepRunning = true;
    }
    else if (keepRunning)
    {
        for (int c = 0; c < kMaxChannels; ++c)
            for (int i = 0; i < 2; ++i)
            {
                keepLow[c][i].clear();
                keepHigh[c][i].clear();
            }

        keepRunning = false;
    }

    keepNow.aim (g.keep, perSample, now);

    // The texture. Each part is left out altogether at 0.
    to (g.crush, clamp01 (s.crush), slowCoef);
    to (g.downsample, clamp01 (s.downsample), slowCoef);
    to (g.distortion, clamp01 (s.distortion), slowCoef);

    crushStep = (s.crush <= 0.0f && g.crush < 1.0e-3f) ? 0.0f : std::pow (2.0f, 1.0f - crushBits (g.crush));
    holdStep = (s.downsample <= 0.0f && g.downsample < 1.0e-3f) ? 1.0f
                                                                : std::min (1.0f, holdHz (g.downsample) / (float) sampleRate);

    if (s.distortion <= 0.0f && g.distortion < 1.0e-3f)
    {
        distortionBlend = 0.0f;
    }
    else
    {
        // Up to 36 dB into the curve, and most of it taken back after.
        distortionGain = std::pow (10.0f, g.distortion * 36.0f / 20.0f);
        distortionLevel = std::pow (distortionGain, -0.35f);
        distortionBlend = std::min (1.0f, g.distortion * 10.0f);
    }

    // The settings as they are heard just now.
    Settings heard = s;
    heard.circuit = analog;
    heard.drive = g.drive;
    heard.feedback = g.feedback;

    driveNow = driveOf (g.drive);
    noiseNow = analog * g.noise * g.noise * 0.004f;
    crossNow = numOut > 1 ? g.cross : 0.0f;

    dryGain.aim (std::cos (g.mix * (float) (0.5 * kPi)), perSample, now);
    wetGain.aim (std::sin (g.mix * (float) (0.5 * kPi)) * driveMakeup (heard) * feedbackMakeup (heard), perSample, now);
    feedbackNow.aim (feedbackGain (heard), perSample, now);
    outGain.aim (g.out, perSample, now);
    bypassNow.aim (g.bypass, perSample, now);

    // The voices.
    const auto count = std::min ((int) kMaxVoices, std::max (1, s.voices));
    const auto centre = (double) g.delayMs * 0.001 * sampleRate;
    const auto depth = 0.95f * g.depth * g.depth;
    const auto stages = stagesOf (std::min (kNumStageChoices - 1, std::max (0, s.stages)));

    for (int k = 0; k < kMaxVoices; ++k)
    {
        auto& v = voices[k];
        const auto on = k < count;
        const auto was = v.gain.target;

        to (v.gainNow, on ? 1.0f : 0.0f, gainCoef);

        if (! on && v.gainNow < 1.0e-4f)
            v.gainNow = 0.0f;

        v.gain.aim (v.gainNow, perSample, now);

        if (v.gainNow == 0.0f && (was == 0.0f || now))
        {
            v.idle = true;
            continue;
        }

        const auto fresh = v.idle || now;
        v.idle = false;

        // The clock of this voice, wandering by itself.
        v.wow += wowCoef * (random() - v.wow);
        v.flutter += flutterCoef * (random() - v.flutter);

        const auto wow = std::min (2.5f, std::max (-2.5f, v.wow * wowScale));
        const auto flutter = std::min (2.5f, std::max (-2.5f, v.flutter * flutterScale));
        const auto clock = 1.0 + (double) (analog * g.drift * (0.02f * wow + 0.003f * flutter));
        const auto middle = centre * (1.0 + 0.5 * g.spread * kStagger[k]) * clock;

        for (int c = 0; c < numOut; ++c)
        {
            const auto offset = (double) k / count + (c == 1 ? g.stereo / 360.0 : 0.0);
            const auto sweep = (1.0f - g.lfo2) * lfo (s.shape, phase1 + offset) + g.lfo2 * lfo (s.shape, phase2 + offset);
            const auto wanted = std::min (maxDelay, std::max (minDelay, middle * (1.0 + (double) (depth * sweep))));

            if (fresh)
            {
                v.delay[c] = v.delayAim[c] = wanted;
                v.delayStep[c] = 0.0;
                v.filter[c].clear();
            }
            else
            {
                v.delay[c] = v.delayAim[c];
                v.delayAim[c] += voiceCoef * (wanted - v.delayAim[c]);
                v.delayStep[c] = (v.delayAim[c] - v.delay[c]) * perSample;
            }

            if (analog > 0.0f)
                v.filter[c].set (bbdCutoffHz (stages, v.delayAim[c] / sampleRate, sampleRate), kBbdFilterQ, sampleRate);
        }
    }

    const auto advance = (double) control / sampleRate;

    phase1 += std::min (40.0f, std::max (0.0f, s.rateHz)) * advance;
    phase2 += std::min (40.0f, std::max (0.0f, s.rate2Hz)) * advance;
    phase1 -= std::floor (phase1);
    phase2 -= std::floor (phase2);

    snap = false;
}

float Modulator::read (const std::vector<float>& line, double delay) const noexcept
{
    const auto whole = (int) delay;
    const auto at = (1.0 - (delay - whole)) * kPhases;      // 0 < at <= kPhases
    const auto row = std::min (kPhases - 1, (int) at);
    const auto blend = (float) (at - row);

    const auto& table = sincTable();
    const auto* h0 = table.h[row];
    const auto* h1 = table.h[row + 1];
    const auto* data = line.data();
    const auto first = writeAt - whole - kHalf;

    float a = 0.0f, b = 0.0f;

    for (int j = 0; j < kTaps; ++j)
    {
        const auto sample = data[(first + j) & mask];
        a += h0[j] * sample;
        b += h1[j] * sample;
    }

    return a + blend * (b - a);
}

void Modulator::process (const float* const* in, float* const* out, int numSamples) noexcept
{
    const auto channels = numOut;

    for (int i = 0; i < numSamples; ++i)
    {
        if (countdown == 0)
        {
            tick();
            countdown = control;
        }

        --countdown;

        float x[kMaxChannels], low[kMaxChannels], high[kMaxChannels], wet[kMaxChannels];

        // Below the keep frequency the input goes round everything.
        const auto keep = keepNow.next();

        for (int c = 0; c < channels; ++c)
        {
            x[c] = in[std::min (c, numIn - 1)][i];

            if (keepRunning)
            {
                const auto l = keepLow[c][1].low (keepLow[c][0].low (x[c]));
                const auto h = keepHigh[c][1].high (keepHigh[c][0].high (x[c]));

                low[c] = keep * l;
                high[c] = x[c] + keep * (h - x[c]);
            }
            else
            {
                low[c] = 0.0f;
                high[c] = x[c];
            }

            wet[c] = 0.0f;
        }

        // Every voice reads the line where its own sweep has taken it.
        float total = 0.0f;

        for (auto& v : voices)
        {
            if (v.idle)
                continue;

            const auto gain = v.gain.next();
            total += gain;

            for (int c = 0; c < channels; ++c)
            {
                v.delay[c] += v.delayStep[c];

                auto sample = read (lines[c], v.delay[c]);

                if (analog > 0.0f)
                    sample += analog * (v.filter[c].low (sample) - sample);

                wet[c] += gain * sample;
            }
        }

        // What goes back in is the voices' mean, so that the loop dies away
        // however many of them there are; what is heard is thicker with each.
        total = std::max (1.0f, total);

        const auto feedback = feedbackNow.next() / total;
        const auto level = wetGain.next();
        const auto thickness = 1.0f / std::sqrt (total);
        const auto dry = dryGain.next();
        const auto volume = outGain.next();
        const auto bypass = bypassNow.next();

        for (int c = 0; c < channels; ++c)
        {
            const auto other = wet[channels > 1 ? 1 - c : c];
            auto written = high[c] + feedback * (wet[c] + crossNow * (other - wet[c]));

            if (analog > 0.0f)
            {
                written += noiseNow * random();
                written += analog * (std::tanh (driveNow * written) / driveNow - written);
            }

            // Too small to count, or not a number at all: neither stays in the loop.
            if (! (std::abs (written) > 1.0e-18f))
                written = 0.0f;

            lines[c][(size_t) writeAt] = std::min (8.0f, std::max (-8.0f, written));
        }

        writeAt = (writeAt + 1) & mask;

        // The texture, on what is heard of the voices and not on what goes
        // back into the line. A lower sample rate is the last sample taken,
        // held until the next one is due.
        auto take = true;

        if (holdStep < 1.0f)
        {
            holdPhase += holdStep;
            take = holdPhase >= 1.0f;

            if (take)
                holdPhase -= 1.0f;
        }

        for (int c = 0; c < channels; ++c)
        {
            auto heard = wet[c] * thickness;

            if (take)
                held[c] = heard;
            else
                heard = held[c];

            if (crushStep > 0.0f)
                heard = crushStep * std::nearbyint (heard / crushStep);

            if (distortionBlend > 0.0f)
                heard += distortionBlend * (distortionLevel * std::tanh (distortionGain * heard) - heard);

            const auto processed = volume * (low[c] + dry * high[c] + level * heard);
            out[c][i] = x[c] + (1.0f - bypass) * (processed - x[c]);
        }
    }
}

} // namespace mdx

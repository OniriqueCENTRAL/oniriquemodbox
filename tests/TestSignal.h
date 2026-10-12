// Signals and measurements for the offline tests and the snapshot tool: no dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mdxtest
{

/** Deterministic white noise in -1..1. */
struct Noise
{
    explicit Noise (uint32_t seed = 0x1234567u) : state (seed) {}

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (int32_t) state / 2147483648.0f;
    }

    uint32_t state;
};

inline std::vector<float> makeNoise (double seconds, double sampleRate, float amplitude, uint32_t seed = 1)
{
    Noise noise (seed);
    std::vector<float> out ((size_t) (seconds * sampleRate));

    for (auto& v : out)
        v = amplitude * noise.next();

    return out;
}

inline std::vector<float> makeTone (double seconds, double sampleRate, double hz, float amplitude)
{
    std::vector<float> out ((size_t) (seconds * sampleRate));

    for (size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * (float) std::sin (2.0 * M_PI * hz * (double) i / sampleRate);

    return out;
}

/** Band-limited sawtooth through three fixed resonators: the fundamental
    moves, the formants do not, which is what tells a pitch shift from a
    formant shift. */
inline std::vector<float> makeVoice (double sampleRate, double seconds, double f0,
                                     double vibratoCents = 0.0, double vibratoHz = 0.0)
{
    const auto n = (int64_t) (sampleRate * seconds);
    std::vector<float> x ((size_t) n, 0.0f);

    double phase = 0.0;

    for (int64_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sampleRate;
        const double cents = vibratoCents * std::sin (2.0 * M_PI * vibratoHz * t);
        const double f = f0 * std::pow (2.0, cents / 1200.0);

        phase += f / sampleRate;

        if (phase >= 1.0)
            phase -= 1.0;

        double s = 0.0;
        const int harmonics = std::min ((int) (0.45 * sampleRate / f), 60);

        for (int h = 1; h <= harmonics; ++h)
            s += std::sin (2.0 * M_PI * h * phase) / (double) h;

        x[(size_t) i] = (float) (0.3 * s);
    }

    for (const double fc : { 700.0, 1220.0, 2600.0 })
    {
        const double r = 0.97;
        const double theta = 2.0 * M_PI * fc / sampleRate;
        const double a1 = -2.0 * r * std::cos (theta);
        const double a2 = r * r;
        double z1 = 0.0, z2 = 0.0;

        for (auto& v : x)
        {
            const double out = (double) v - a1 * z1 - a2 * z2;
            z2 = z1;
            z1 = out;
            v = (float) (0.3 * out);
        }
    }

    return x;
}

/** The level of one frequency in a stretch of signal, as an amplitude. */
inline double toneLevel (const float* x, size_t count, double hz, double sampleRate)
{
    double re = 0.0, im = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) count);
        const double phase = 2.0 * M_PI * hz * (double) i / sampleRate;
        re += w * x[i] * std::cos (phase);
        im -= w * x[i] * std::sin (phase);
    }

    return 4.0 * std::sqrt (re * re + im * im) / (double) count;
}

inline double rmsDb (const float* x, size_t count)
{
    double sum = 0.0;

    for (size_t i = 0; i < count; ++i)
        sum += (double) x[i] * x[i];

    return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, count) + 1.0e-20);
}

/** The fundamental of a stretch of signal, in Hz: the shortest lag at which
    it matches itself nearly as well as it ever does. */
inline double pitchOf (const float* x, size_t count, double sampleRate)
{
    const int minLag = (int) (sampleRate / 1200.0);
    const int maxLag = std::min ((int) (sampleRate / 60.0), (int) count / 2);
    const size_t length = count - (size_t) maxLag;

    std::vector<double> match ((size_t) maxLag + 2, 0.0);
    double highest = 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0, a = 1.0e-20, b = 1.0e-20;

        for (size_t i = 0; i < length; ++i)
        {
            sum += (double) x[i] * x[i + (size_t) lag];
            a += (double) x[i] * x[i];
            b += (double) x[i + (size_t) lag] * x[i + (size_t) lag];
        }

        match[(size_t) lag] = sum / std::sqrt (a * b);
        highest = std::max (highest, match[(size_t) lag]);
    }

    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const auto here = match[(size_t) lag];

        if (here >= 0.93 * highest && here >= match[(size_t) lag - 1] && here >= match[(size_t) lag + 1])
        {
            const auto curve = match[(size_t) lag - 1] - 2.0 * here + match[(size_t) lag + 1];
            const auto fine = curve < 0.0 ? 0.5 * (match[(size_t) lag - 1] - match[(size_t) lag + 1]) / curve : 0.0;

            return sampleRate / ((double) lag + fine);
        }
    }

    return 0.0;
}

/** Where the weight of a voice's harmonics lies, in Hz. Moving the pitch
    with the formants kept leaves it nearly where it was; moving the formants
    takes it with them. */
inline double harmonicCentroid (const float* x, size_t count, double f0, double sampleRate)
{
    double weighted = 0.0, total = 0.0;

    for (int h = 1; h * f0 < 5000.0; ++h)
    {
        const auto level = toneLevel (x, count, h * f0, sampleRate);
        weighted += h * f0 * level;
        total += level;
    }

    return total > 0.0 ? weighted / total : 0.0;
}

} // namespace mdxtest

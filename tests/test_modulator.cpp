// The whole audio path, offline: what the plugin does to a signal is decided
// in Modulator, and this is where it is held to it.
#include "dsp/Modulator.h"
#include "TestSignal.h"

#include <chrono>
#include <cstdio>

namespace
{
constexpr double kSr = 48000.0;
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);

    if (! ok)
        ++failures;
}

struct Stereo { std::vector<float> left, right; };

/** Runs a signal through: one channel in, or two when a second is given. */
Stereo run (const mdx::Settings& settings, const std::vector<float>& left, int block = 512,
            const std::vector<float>* right = nullptr)
{
    mdx::Modulator modulator;
    modulator.setSettings (settings);
    modulator.prepare (kSr, right != nullptr ? 2 : 1, 2);

    Stereo out { left, right != nullptr ? *right : left };

    for (size_t at = 0; at < left.size(); at += (size_t) block)
    {
        const auto count = (int) std::min ((size_t) block, left.size() - at);
        float* io[2] = { out.left.data() + at, out.right.data() + at };

        modulator.process (io, io, count);
    }

    return out;
}

/** The largest difference between the output and the input delayed, in dB. */
double worstErrorDb (const std::vector<float>& in, const std::vector<float>& out, int delay)
{
    double worst = 0.0;

    for (size_t i = (size_t) delay; i < out.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - in[i - (size_t) delay]));

    return 20.0 * std::log10 (worst + 1.0e-12);
}

double worstDifferenceDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));

    return 20.0 * std::log10 (worst + 1.0e-12);
}

double peakOf (const std::vector<float>& x)
{
    double peak = 0.0;

    for (const auto v : x)
        peak = std::isfinite (v) ? std::max (peak, (double) std::abs (v)) : 1.0e9;

    return peak;
}

double db (double amplitude) { return 20.0 * std::log10 (amplitude + 1.0e-12); }

/** The level of a tone over the last second of a signal. */
double toneDb (const std::vector<float>& x, double hz)
{
    const auto span = (size_t) kSr;
    return db (mdxtest::toneLevel (x.data() + x.size() - span, span, hz, kSr));
}

/** One voice that does not move, heard by itself: a plain delay. */
mdx::Settings still (float delayMs)
{
    mdx::Settings s;
    s.circuit = 0.0f;
    s.voices = 1;
    s.depth = 0.0f;
    s.feedback = 0.0f;
    s.mix = 1.0f;
    s.delayMs = delayMs;
    return s;
}
}

int main()
{
    const auto noise = mdxtest::makeNoise (3.0, kSr, 0.25f);
    const auto silence = std::vector<float> ((size_t) (2.0 * kSr), 0.0f);

    std::printf ("Digital\n");
    {
        auto s = still (5.0f);
        s.mix = 0.0f;
        s.depth = 1.0f;
        s.feedback = 0.9f;
        check (worstErrorDb (noise, run (s, noise).left, 0) < -140.0, "dry, what comes out is what went in");

        // 5 ms is 240 samples: a whole number, where the sinc is one sample.
        const auto delayed = run (still (5.0f), noise);
        const auto error = worstErrorDb (noise, delayed.left, 240);
        std::printf ("    a still voice against the input 240 samples late: %.1f dB\n", error);
        check (error < -100.0, "wet, a voice that does not move is a delay to the sample");

        // Between two samples: a tone has to come out at its level, as late
        // as asked. 2.51 ms is 120.48 samples.
        const auto tone = mdxtest::makeTone (2.0, kSr, 5000.0, 0.5f);
        const auto between = run (still (2.51f), tone).left;
        double worst = 0.0;

        for (size_t i = between.size() - 4800; i < between.size(); ++i)
        {
            const auto expected = 0.5 * std::sin (2.0 * M_PI * 5000.0 * ((double) i - 120.48) / kSr);
            worst = std::max (worst, std::abs (between[i] - expected));
        }

        std::printf ("    5 kHz read between two samples, against the same tone worked out: %.1f dB\n", db (worst));
        check (db (worst) < -60.0, "a delay between two samples is that delay");

        check (peakOf (run (still (5.0f), silence).left) == 0.0, "silence in, silence out: no noise of its own");
    }

    std::printf ("\nComb\n");
    {
        // 1 ms against the dry: notches at 500 Hz, 1.5 kHz..., peaks at 1 kHz, 2 kHz...
        auto s = still (1.0f);
        s.mix = 0.5f;

        const auto notch = toneDb (run (s, mdxtest::makeTone (2.0, kSr, 500.0, 0.5f)).left, 500.0);
        const auto peak = toneDb (run (s, mdxtest::makeTone (2.0, kSr, 1000.0, 0.5f)).left, 1000.0);
        std::printf ("    1 ms, half wet: %.1f dB at 500 Hz, %.1f dB at 1 kHz\n", notch, peak);
        check (peak - notch > 50.0, "a voice against the dry is a comb");

        // Feedback the other way round moves the peaks to where the notches were.
        auto positive = still (1.0f), negative = still (1.0f);
        positive.feedback = 0.8f;
        negative.feedback = -0.8f;

        const auto tone = mdxtest::makeTone (2.0, kSr, 500.0, 0.1f);
        const auto up = toneDb (run (positive, tone).left, 500.0), down = toneDb (run (negative, tone).left, 500.0);
        std::printf ("    500 Hz through 1 ms: %.1f dB with the feedback at +80 %%, %.1f dB at -80 %%\n", up, down);
        check (down - up > 12.0, "inverting the feedback turns the comb over");
    }

    std::printf ("\nStability\n");
    {
        for (const auto circuit : { 0.0f, 0.5f, 1.0f })
            for (const auto feedback : { 1.0f, -1.0f })
            {
                mdx::Settings s;
                s.circuit = circuit;
                s.drive = 1.0f;
                s.drift = 1.0f;
                s.noise = 1.0f;
                s.voices = 8;
                s.spread = 1.0f;
                s.depth = 1.0f;
                s.rateHz = 7.0f;
                s.lfo2 = 0.5f;
                s.rate2Hz = 13.0f;
                s.feedback = feedback;
                s.cross = 0.5f;
                s.mix = 1.0f;
                s.stereoDeg = 180.0f;
                s.keepHz = 200.0f;
                s.crush = s.downsample = s.distortion = 0.8f;

                const auto loud = mdxtest::makeNoise (6.0, kSr, 1.0f, 7);
                const auto out = run (s, loud);
                const auto peak = std::max (peakOf (out.left), peakOf (out.right));

                // Then the input stops, and the loop has to die away.
                auto tail = loud;
                std::fill (tail.begin() + (long) kSr, tail.end(), 0.0f);
                s.noise = 0.0f;
                const auto after = run (s, tail).left;
                const auto left = mdxtest::rmsDb (after.data() + after.size() - 4800, 4800);

                std::printf ("    circuit %.0f %%, feedback %+.0f %%: peak %.1f dB, %.1f dB five seconds after the input stops\n",
                             circuit * 100.0f, feedback * 100.0f, db (peak), left);
                check (peak < 40.0 && left < -60.0, "everything at its most, and the loop still dies away");
            }
    }

    std::printf ("\nAnalog\n");
    {
        // The same long delay, with a tone high up: the bucket brigade takes
        // it down, and more so with fewer stages.
        const auto tone = mdxtest::makeTone (2.0, kSr, 9000.0, 0.05f);
        auto s = still (30.0f);
        const auto digital = toneDb (run (s, tone).left, 9000.0);

        s.circuit = 1.0f;
        s.drive = 0.0f;
        s.drift = 0.0f;
        s.stages = 2;
        const auto analog = toneDb (run (s, tone).left, 9000.0);

        s.stages = 0;
        const auto fewer = toneDb (run (s, tone).left, 9000.0);

        s.stages = 2;
        s.delayMs = 2.0f;
        const auto shorter = toneDb (run (s, tone).left, 9000.0);

        std::printf ("    9 kHz through 30 ms: %.1f dB digital, %.1f dB on 1024 stages, %.1f dB on 256; through 2 ms: %.1f dB\n",
                     digital, analog, fewer, shorter);
        check (analog < digital - 6.0 && fewer < analog - 12.0, "a longer delay on fewer stages is a duller one");
        check (shorter > digital - 1.5, "and a short one is left nearly open");

        // Driven hard, a loud tone comes out with harmonics it did not have.
        const auto loud = mdxtest::makeTone (2.0, kSr, 440.0, 0.9f);
        auto driven = still (5.0f);
        driven.circuit = 1.0f;
        driven.drive = 1.0f;
        driven.drift = 0.0f;

        const auto third = toneDb (run (driven, loud).left, 1320.0) - toneDb (run (driven, loud).left, 440.0);
        const auto clean = toneDb (run (still (5.0f), loud).left, 1320.0) - toneDb (run (still (5.0f), loud).left, 440.0);
        std::printf ("    third harmonic of a loud 440 Hz: %.1f dB under it driven, %.1f dB digital\n", -third, -clean);
        check (third > -30.0 && clean < -80.0, "the drive saturates, and digital does not");

        // Hiss, when asked for, and only then.
        auto hiss = still (5.0f);
        hiss.circuit = 1.0f;
        const auto quiet = peakOf (run (hiss, silence).left);
        hiss.noise = 1.0f;
        const auto noisy = run (hiss, silence).left;
        const auto level = mdxtest::rmsDb (noisy.data() + noisy.size() - 4800, 4800);
        std::printf ("    hiss at its most: %.1f dB\n", level);
        check (quiet == 0.0 && level > -90.0 && level < -40.0, "there is hiss when asked for, and none otherwise");

        // The clock wanders: the same tone, twice, is not the same twice over time.
        auto wander = still (15.0f);
        wander.circuit = 1.0f;
        wander.drift = 1.0f;
        wander.drive = 0.0f;
        const auto steady = mdxtest::makeTone (3.0, kSr, 1000.0, 0.2f);
        auto fixed = wander;
        fixed.drift = 0.0f;
        check (worstDifferenceDb (run (wander, steady).left, run (fixed, steady).left) > -40.0, "drift moves the delay by itself");
    }

    std::printf ("\nTexture\n");
    {
        const auto tone = mdxtest::makeTone (2.0, kSr, 440.0, 0.5f);
        const auto span = (size_t) kSr;

        // Three bits: whatever goes in, only a handful of levels come out.
        auto crushed = still (5.0f);
        crushed.crush = 1.0f;
        auto levels = run (crushed, tone).left;
        levels.erase (levels.begin(), levels.end() - (long) span);

        // To the nearest ten-thousandth: the last bit of a sum is not a level.
        for (auto& v : levels)
            v = std::round (v * 1.0e4f);

        std::sort (levels.begin(), levels.end());
        const auto distinct = std::unique (levels.begin(), levels.end()) - levels.begin();
        std::printf ("    a tone on three bits: %d levels\n", (int) distinct);
        check (distinct >= 3 && distinct <= 9, "crushed, the voices are on a few bits");

        // Held at 300 Hz, the output moves 300 times a second and no more.
        auto slowed = still (5.0f);
        slowed.downsample = 1.0f;
        const auto stepped = run (slowed, tone).left;
        int changes = 0;

        for (size_t i = stepped.size() - span + 1; i < stepped.size(); ++i)
            changes += std::abs (stepped[i] - stepped[i - 1]) > 1.0e-4f ? 1 : 0;

        std::printf ("    a tone held at 300 Hz: %d changes in a second\n", changes);
        check (changes > 250 && changes <= 301, "downsampled, the voices are held between two samples taken");

        // Distorted, with the circuit still digital.
        auto distorted = still (5.0f);
        distorted.distortion = 0.7f;
        const auto third = toneDb (run (distorted, tone).left, 1320.0) - toneDb (run (distorted, tone).left, 440.0);
        std::printf ("    third harmonic of 440 Hz distorted: %.1f dB under it\n", -third);
        check (third > -20.0, "the distortion distorts, whatever the circuit");

        // And none of it touches the dry sound.
        auto dry = distorted;
        dry.crush = dry.downsample = 1.0f;
        dry.mix = 0.0f;
        check (worstErrorDb (tone, run (dry, tone).left, 0) < -140.0, "the dry sound is left as it is");
    }

    std::printf ("\nDensity\n");
    {
        // Bass kept: a low tone comes out as it went in, whatever is done above.
        mdx::Settings s;
        s.voices = 4;
        s.depth = 1.0f;
        s.rateHz = 3.0f;
        s.feedback = -0.7f;
        s.mix = 1.0f;
        s.delayMs = 6.0f;
        s.keepHz = 300.0f;

        const auto bass = mdxtest::makeTone (3.0, kSr, 60.0, 0.5f);
        const auto kept = run (s, bass).left;
        const auto level = toneDb (kept, 60.0) - db (0.5);
        std::printf ("    60 Hz with the bass kept under 300 Hz, all wet: %+.2f dB\n", level);
        check (std::abs (level) < 0.5, "the bass that is kept comes out at its level");

        // Dry, the two halves of the crossover add up to the input's level.
        s.mix = 0.0f;
        const auto across = mdxtest::makeTone (3.0, kSr, 300.0, 0.5f);
        check (std::abs (toneDb (run (s, across).left, 300.0) - db (0.5)) < 0.1, "and the crossover is flat where it crosses");

        // More voices: thicker, not just louder, and not much louder either.
        mdx::Settings one, eight;
        one.mix = eight.mix = 1.0f;
        one.feedback = eight.feedback = 0.0f;
        one.delayMs = eight.delayMs = 15.0f;
        eight.voices = 8;
        eight.spread = 1.0f;

        const auto thin = run (one, noise).left, thick = run (eight, noise).left;
        const auto gain = mdxtest::rmsDb (thick.data() + 48000, 96000) - mdxtest::rmsDb (thin.data() + 48000, 96000);
        std::printf ("    eight voices against one: %+.1f dB\n", gain);
        check (std::abs (gain) < 3.0, "eight voices are about as loud as one");
    }

    std::printf ("\nStereo\n");
    {
        mdx::Settings s;
        s.depth = 0.8f;
        s.rateHz = 2.0f;
        s.voices = 3;
        s.stereoDeg = 0.0f;

        const auto centred = run (s, noise);
        check (worstDifferenceDb (centred.left, centred.right) < -140.0, "with no offset between the LFOs, one channel in is the same on both sides");

        s.stereoDeg = 180.0f;
        const auto wide = run (s, noise);
        check (worstDifferenceDb (wide.left, wide.right) > -30.0, "with one, the two sides differ");

        // Two different channels stay two channels.
        const auto other = mdxtest::makeNoise (3.0, kSr, 0.25f, 99);
        s.mix = 0.0f;
        const auto two = run (s, noise, 512, &other);
        check (worstErrorDb (noise, two.left, 0) < -140.0 && worstErrorDb (other, two.right, 0) < -140.0, "two channels in are kept apart");
    }

    std::printf ("\nBlocks\n");
    {
        mdx::Settings s;
        s.circuit = 0.7f;
        s.voices = 5;
        s.depth = 0.9f;
        s.rateHz = 1.3f;
        s.feedback = 0.6f;
        s.keepHz = 150.0f;

        check (worstDifferenceDb (run (s, noise, 512).left, run (s, noise, 37).left) < -140.0, "the size of the blocks changes nothing");

        // Bypass is the input.
        s.bypass = true;
        check (worstErrorDb (noise, run (s, noise).left, 0) < -120.0, "bypassed, what comes out is what went in");
    }

    std::printf ("\nAuto gain\n");
    {
        // The level over the last second, once the followers have settled.
        auto levelDb = [] (const std::vector<float>& x)
        {
            double sum = 0.0;
            const auto from = x.size() - (size_t) kSr;

            for (auto i = from; i < x.size(); ++i)
                sum += (double) x[i] * x[i];

            return 10.0 * std::log10 (sum / kSr + 1.0e-20);
        };

        const auto in = levelDb (noise);

        // A setting that is louder than what goes in.
        auto loud = still (3.0f);
        loud.voices = 4;
        loud.feedback = 0.9f;
        loud.mix = 0.7f;
        loud.distortion = 1.0f;
        const auto without = levelDb (run (loud, noise).left) - in;
        loud.autoGain = true;
        const auto with = levelDb (run (loud, noise).left) - in;
        std::printf ("        (%.1f dB without, %.2f dB with)\n", without, with);
        check (std::abs (without) > 0.5 && std::abs (with) < 0.2, "a setting that changes the level comes out at the level of the input");

        // And one that is quieter: all wet, crushed, the bass kept out of it.
        auto quiet = still (5.0f);
        quiet.mix = 1.0f;
        quiet.feedback = -0.6f;
        quiet.keepHz = 400.0f;
        quiet.crush = 1.0f;
        const auto quietWithout = levelDb (run (quiet, noise).left) - in;
        quiet.autoGain = true;
        const auto quietWith = levelDb (run (quiet, noise).left) - in;
        std::printf ("        (%.1f dB without, %.2f dB with)\n", quietWithout, quietWith);
        check (std::abs (quietWith) < 0.5, "whichever way the level went");

        // The output gain is the user's, and is left on top.
        loud.outDb = -6.0f;
        check (std::abs (levelDb (run (loud, noise).left) - in + 6.0) < 0.5, "the output gain comes on top of it");

        auto dry = still (5.0f);
        dry.mix = 0.0f;
        dry.autoGain = true;
        check (worstErrorDb (noise, run (dry, noise).left, 0) < -120.0, "with nothing to make up for, it does nothing");
        check (peakOf (run (loud, silence).left) == 0.0, "and silence stays silence");
    }

    std::printf ("\nSpeed\n");
    {
        mdx::Settings s;
        s.circuit = 1.0f;
        s.voices = 8;
        s.depth = 1.0f;
        s.feedback = 0.8f;
        s.keepHz = 200.0f;
        s.noise = 0.5f;

        const auto long_ = mdxtest::makeNoise (20.0, kSr, 0.25f);
        const auto start = std::chrono::steady_clock::now();
        const auto out = run (s, long_);
        const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

        std::printf ("    eight analog voices in stereo: %.1f %% of real time (peak %.2f)\n", 100.0 * seconds / 20.0, peakOf (out.left));
        check (seconds < 4.0, "eight voices run well inside real time");
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

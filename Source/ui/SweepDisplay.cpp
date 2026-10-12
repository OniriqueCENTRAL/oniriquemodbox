#include "SweepDisplay.h"

#include <complex>

#include "Theme.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kLowHz = 20.0f, kHighHz = 20000.0f;
    constexpr float kTopDb = 18.0f, kBottomDb = -30.0f;
    constexpr float kShortMs = 0.2f, kLongMs = 100.0f;

    using Complex = std::complex<float>;

    /** The voices as they stand, and what is needed to work the response out. */
    struct Scene
    {
        mdx::Settings settings;
        double sampleRate = 48000.0;
        int count = 0;
        float gain[mdx::kMaxVoices] = {};
        float delayMs[mdx::kMaxVoices][2] = {};
        float cutoff[mdx::kMaxVoices][2] = {};
        float total = 1.0f;
    };

    Scene sceneOf (const ModBoxProcessor& processor)
    {
        Scene scene;
        scene.settings = processor.readSettings();
        scene.sampleRate = processor.getDisplaySampleRate();

        float heard = 0.0f;

        for (int k = 0; k < mdx::kMaxVoices; ++k)
            heard += processor.getVoiceGain (k);

        // Before any sound has gone through, the voices are drawn where they
        // would rest.
        const auto running = heard > 0.0f;
        float total = 0.0f;

        for (int k = 0; k < mdx::kMaxVoices; ++k)
        {
            const auto gain = running ? processor.getVoiceGain (k) : (k < scene.settings.voices ? 1.0f : 0.0f);

            if (gain <= 0.001f)
                continue;

            const auto i = scene.count++;
            scene.gain[i] = gain;
            total += gain;

            for (int c = 0; c < 2; ++c)
            {
                scene.delayMs[i][c] = running ? processor.getVoiceDelayMs (k, c) : mdx::restDelayMs (scene.settings, k);
                scene.cutoff[i][c] = (float) mdx::bbdCutoffHz (mdx::stagesOf (scene.settings.stages),
                                                               scene.delayMs[i][c] * 0.001, scene.sampleRate);
            }
        }

        scene.total = jmax (1.0f, total);
        return scene;
    }

    /** What comes out of each side for the same thing put into both, at one frequency. */
    void responseAt (const Scene& scene, float hz, float& leftDb, float& rightDb)
    {
        const auto& s = scene.settings;
        const auto analog = jlimit (0.0f, 1.0f, s.circuit);
        Complex taps[2];

        for (int i = 0; i < scene.count; ++i)
            for (int c = 0; c < 2; ++c)
            {
                auto voice = std::polar (scene.gain[i], -MathConstants<float>::twoPi * hz * scene.delayMs[i][c] * 0.001f);

                if (analog > 0.0f)
                {
                    const auto r = hz / scene.cutoff[i][c];
                    const auto filter = 1.0f / Complex (1.0f - r * r, r / (float) mdx::kBbdFilterQ);
                    voice *= (1.0f - analog) + analog * filter;
                }

                taps[c] += voice;
            }

        // The loop, with whatever of it crosses from one side to the other.
        const auto feedback = mdx::feedbackGain (s);
        const auto cross = jlimit (0.0f, 1.0f, s.cross);
        const Complex mean[2] = { taps[0] / scene.total, taps[1] / scene.total };
        const auto own = feedback * (1.0f - cross), across = feedback * cross;
        const auto determinant = (1.0f - own * mean[0]) * (1.0f - own * mean[1]) - across * across * mean[0] * mean[1];
        const Complex written[2] = { (1.0f - (own - across) * mean[1]) / determinant,
                                     (1.0f - (own - across) * mean[0]) / determinant };

        const auto dry = std::cos (s.mix * MathConstants<float>::halfPi);
        const auto wet = std::sin (s.mix * MathConstants<float>::halfPi) * mdx::driveMakeup (s) * mdx::feedbackMakeup (s) / std::sqrt (scene.total);
        const auto kept = s.keepHz > 20.5f ? 1.0f / (1.0f + std::pow (hz / s.keepHz, 4.0f)) : 0.0f;
        float* const results[2] = { &leftDb, &rightDb };

        for (int c = 0; c < 2; ++c)
        {
            const auto out = kept + (1.0f - kept) * (dry + wet * taps[c] * written[c]);
            *results[c] = Decibels::gainToDecibels (std::abs (out), -100.0f) + s.outDb;
        }
    }
}

SweepDisplay::SweepDisplay (ModBoxProcessor& p) : processor (p)
{
    setOpaque (false);
}

void SweepDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (deep);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (line);
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    auto inner = bounds.reduced (14.0f, 10.0f);
    auto strip = inner.removeFromBottom (jmin (52.0f, inner.getHeight() * 0.28f));
    inner.removeFromBottom (8.0f);
    const auto plot = inner;

    const auto scene = sceneOf (processor);
    const auto bypassed = scene.settings.bypass;

    auto xOfHz = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / kLowHz) / std::log (kHighHz / kLowHz); };
    auto yOfDb = [plot] (float db) { return plot.getY() + plot.getHeight() * (kTopDb - db) / (kTopDb - kBottomDb); };

    // The grid: the decades, and every twelve dB.
    g.setFont (monoFont (9.0f));

    for (const auto& [hz, name] : std::initializer_list<std::pair<float, const char*>> { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
    {
        const auto x = xOfHz (hz);

        g.setColour (line);
        g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
        g.setColour (haze.withAlpha (0.7f));
        g.drawText (name, Rectangle<float> (x + 4.0f, plot.getBottom() - 12.0f, 40.0f, 10.0f), Justification::centredLeft, false);
    }

    for (const auto db : { 12.0f, 0.0f, -12.0f, -24.0f })
    {
        const auto y = yOfDb (db);

        g.setColour (db == 0.0f ? Colour (0x40ffd2d6) : line);
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
        g.setColour (haze.withAlpha (0.7f));
        g.drawText ((db > 0.0f ? "+" : "") + String (roundToInt (db)), Rectangle<float> (plot.getX() + 2.0f, y - 11.0f, 30.0f, 10.0f),
                    Justification::centredLeft, false);
    }

    // The two curves.
    // One point to the pixel: the comb of a long delay is finer than anything less.
    const auto steps = jlimit (200, 1600, roundToInt (plot.getWidth()));
    Path left, right;

    for (int i = 0; i < steps; ++i)
    {
        const auto hz = kLowHz * std::pow (kHighHz / kLowHz, (float) i / (float) (steps - 1));
        float l = 0.0f, r = 0.0f;
        responseAt (scene, hz, l, r);

        const auto x = xOfHz (hz);
        const auto yl = yOfDb (jlimit (kBottomDb - 6.0f, kTopDb + 6.0f, l));
        const auto yr = yOfDb (jlimit (kBottomDb - 6.0f, kTopDb + 6.0f, r));

        if (i == 0)
        {
            left.startNewSubPath (x, yl);
            right.startNewSubPath (x, yr);
        }
        else
        {
            left.lineTo (x, yl);
            right.lineTo (x, yr);
        }
    }

    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        const auto strength = bypassed ? 0.3f : 1.0f;

        g.setColour (bone.withAlpha (0.5f * strength));
        g.strokePath (right, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (ember.withAlpha (0.2f * strength));
        g.strokePath (left, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (emberLit.withAlpha (strength));
        g.strokePath (left, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setFont (capsFont (9.0f));
    g.setColour (emberLit);
    g.drawText ("L", Rectangle<float> (plot.getRight() - 40.0f, plot.getY() + 2.0f, 16.0f, 12.0f), Justification::centred, false);
    g.setColour (bone.withAlpha (0.7f));
    g.drawText ("R", Rectangle<float> (plot.getRight() - 22.0f, plot.getY() + 2.0f, 16.0f, 12.0f), Justification::centred, false);

    // The voices, on a line of delays: the left side over the right.
    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (strip, 6.0f);

    auto track = strip.reduced (34.0f, 0.0f);
    auto xOfMs = [track] (float ms)
    {
        return track.getX() + track.getWidth() * std::log (jlimit (kShortMs, kLongMs, ms) / kShortMs) / std::log (kLongMs / kShortMs);
    };

    const auto upper = strip.getCentreY() - 8.0f, lower = strip.getCentreY() + 8.0f;

    g.setFont (monoFont (9.0f));

    for (const auto& [ms, name] : std::initializer_list<std::pair<float, const char*>> { { 1.0f, "1 ms" }, { 10.0f, "10 ms" } })
    {
        const auto x = xOfMs (ms);

        g.setColour (line);
        g.fillRect (x, strip.getY() + 4.0f, 1.0f, strip.getHeight() - 8.0f);
        g.setColour (haze.withAlpha (0.7f));
        g.drawText (name, Rectangle<float> (x + 4.0f, strip.getBottom() - 13.0f, 50.0f, 10.0f), Justification::centredLeft, false);
    }

    g.setColour (line);
    g.fillRect (track.getX(), upper, track.getWidth(), 1.0f);
    g.fillRect (track.getX(), lower, track.getWidth(), 1.0f);

    g.setFont (capsFont (9.0f));
    g.setColour (haze);
    g.drawText ("L", Rectangle<float> (strip.getX() + 8.0f, upper - 6.0f, 16.0f, 12.0f), Justification::centred, false);
    g.drawText ("R", Rectangle<float> (strip.getX() + 8.0f, lower - 6.0f, 16.0f, 12.0f), Justification::centred, false);

    for (int i = 0; i < scene.count; ++i)
    {
        const auto strength = scene.gain[i] * (bypassed ? 0.3f : 1.0f);
        const Point<float> points[2] = { { xOfMs (scene.delayMs[i][0]), upper }, { xOfMs (scene.delayMs[i][1]), lower } };

        g.setColour (ember.withAlpha (0.35f * strength));
        g.drawLine ({ points[0], points[1] }, 1.0f);

        for (const auto& p : points)
        {
            g.setColour (ember.withAlpha (0.25f * strength));
            g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (p));
            g.setColour (emberLit.withAlpha (strength));
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (p));
        }
    }
}

} // namespace onirique

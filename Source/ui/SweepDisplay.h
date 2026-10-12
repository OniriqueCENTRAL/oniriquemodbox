// Onirique ModBox — the sweep: what the voices do to the spectrum, and where they are
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

namespace onirique
{

/** Two pictures of the same thing. Above, the response of the whole plugin
    as the voices stand just now, one curve for each side: the notches of a
    flanger, the ripple of a chorus, moving as the LFOs do. Below, each voice
    as a point on a line of delays, the left side over the right.

    It only shows; nothing in it is dragged. */
class SweepDisplay final : public Component
{
public:
    explicit SweepDisplay (ModBoxProcessor&);

    void paint (Graphics&) override;

private:
    ModBoxProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepDisplay)
};

} // namespace onirique

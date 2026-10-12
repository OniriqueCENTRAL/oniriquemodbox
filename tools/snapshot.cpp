// Renders the editor to a PNG without a host, with a sound running through
// it, so a change to the interface can be looked at straight from the build.
//
//   modbox_snapshot out.png [preset] [width height]
//   modbox_snapshot out.png --browser       the preset browser open, narrowed to two types
//   modbox_snapshot --selftest      the slots, the presets and the saved state, without a window
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignal.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<onirique::ModBoxProcessor> makeProcessor()
{
    return std::unique_ptr<onirique::ModBoxProcessor> (static_cast<onirique::ModBoxProcessor*> (createPluginFilter()));
}

/** Sets the plugin up, plays into it on the message thread one timer tick at
    a time so the sweep can follow, then takes the picture and ends the
    program. */
struct Director final : juce::Timer
{
    Director (juce::File destination, const juce::String& preset, int width, int height)
        : file (std::move (destination))
    {
        processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        const auto browsing = preset == "--browser";

        if (preset.isNotEmpty() && ! browsing)
            processor->loadPreset (preset);

        signal = mdxtest::makeVoice (kSr, 4.0, 196.0, 40.0, 5.0);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        // The browser open, narrowed to two types, on one of their presets.
        if (browsing)
        {
            using P = onirique::ModBoxProcessor;

            processor->setTypeFilter (P::bitCrusher | P::distortion);
            processor->loadPreset ("Fuzz Flanger");
            modBox = dynamic_cast<onirique::ModBoxEditor*> (editor.get());
        }

        startTimer (30);
    }

    /** A click on the preset's name, as the mouse would make it. */
    void clickName()
    {
        auto& field = modBox->getPresetField();
        const auto at = field.getLocalBounds().getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();

        const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), at,
                                      juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &field, &field, now, at, now, 1, false);

        field.mouseDown (click);
        field.mouseUp (click);
    }

    /** Opened by a click, closed, and opened by a click again: the picture
        is of the second time. */
    void browse()
    {
        if (ticks == 5 || ticks == 25)
            clickName();

        if (ticks == 15)
        {
            std::printf ("  %s  a click on the preset's name opens the browser\n", modBox->isBrowserOpen() ? "ok  " : "FAIL");
            modBox->closeBrowser (true);
        }

        if (ticks == 35)
            std::printf ("  %s  and a click opens it again once it has been closed\n", modBox->isBrowserOpen() ? "ok  " : "FAIL");
    }

    void timerCallback() override
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        // About a tick's worth of audio per tick.
        for (int i = 0; i < 3 && position + kBlock <= signal.size(); ++i)
        {
            buffer.copyFrom (0, 0, signal.data() + position, kBlock);
            buffer.copyFrom (1, 0, signal.data() + position, kBlock);
            processor->processBlock (buffer, midi);
            position += kBlock;
        }

        if (modBox != nullptr)
            browse();

        if (++ticks > 60)
            finish();
    }

    void finish()
    {
        stopTimer();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    juce::File file;
    std::unique_ptr<onirique::ModBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    onirique::ModBoxEditor* modBox = nullptr;       // only when the browser is what is shown
    std::vector<float> signal;
    size_t position = 0;
    int ticks = 0;
};
}

/** What the processor keeps that is not audio: the two slots, the presets,
    and the state a host saves; and the plugin as a host drives it, on one
    channel and on two. Returns the number of things found wrong. */
int selfTest()
{
    int failures = 0;

    auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    auto processor = makeProcessor();

    auto get = [&] (const char* id) { return processor->apvts.getRawParameterValue (id)->load(); };
    auto set = [&] (const char* id, float value)
    {
        auto* p = processor->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // A and B.
    set ("feedback", 30.0f);
    processor->switchToSlot (1);
    check (std::abs (get ("feedback") - 30.0f) < 0.01f, "B starts as a copy of A");
    set ("feedback", -70.0f);
    set ("voices", 6.0f);
    processor->switchToSlot (0);
    check (std::abs (get ("feedback") - 30.0f) < 0.01f && std::abs (get ("voices") - 1.0f) < 0.01f, "back on A, A is as it was left");
    processor->switchToSlot (1);
    check (std::abs (get ("feedback") + 70.0f) < 0.01f && std::abs (get ("voices") - 6.0f) < 0.01f, "and B as it was left");
    processor->copyToOtherSlot();
    processor->switchToSlot (0);
    check (std::abs (get ("feedback") + 70.0f) < 0.01f, "B > A makes A the same as B");

    set ("bypass", 1.0f);
    set ("feedback", 20.0f);
    processor->switchToSlot (1);
    check (get ("bypass") > 0.5f, "switching slots leaves bypass alone");
    set ("bypass", 0.0f);

    // The saved state carries both slots.
    juce::MemoryBlock saved;
    processor->getStateInformation (saved);

    auto second = makeProcessor();
    second->setStateInformation (saved.getData(), (int) saved.getSize());

    auto get2 = [&] (const char* id) { return second->apvts.getRawParameterValue (id)->load(); };
    check (second->getActiveSlot() == 1 && std::abs (get2 ("feedback") + 70.0f) < 0.01f, "a restored state is on the same slot, with its settings");
    second->switchToSlot (0);
    check (std::abs (get2 ("feedback") - 20.0f) < 0.01f, "and has the other slot too");

    // Through the plugin as a host drives it.
    {
        const auto voice = mdxtest::makeVoice (kSr, 2.0, 220.0);

        // All dry, what comes out is what went in.
        auto worst = [&] (int channelsIn, const char* preset)
        {
            auto host = makeProcessor();

            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (channelsIn == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

            if (! host->setBusesLayout (layout))
                return 0.0;

            host->loadPreset (preset);

            auto* mix = host->apvts.getParameter ("mix");
            mix->setValueNotifyingHost (0.0f);

            auto* keep = host->apvts.getParameter ("keep");
            keep->setValueNotifyingHost (0.0f);

            host->setRateAndBufferSizeDetails (kSr, kBlock);
            host->prepareToPlay (kSr, kBlock);

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            double error = 0.0;

            for (size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
            {
                buffer.clear();

                for (int c = 0; c < channelsIn; ++c)
                    buffer.copyFrom (c, 0, voice.data() + at, kBlock);

                host->processBlock (buffer, midi);

                for (int i = 0; i < kBlock; ++i)
                    for (int c = 0; c < 2; ++c)
                        error = std::max (error, (double) std::abs (buffer.getSample (c, i) - voice[at + (size_t) i]));
            }

            return 20.0 * std::log10 (error + 1.0e-12);
        };

        const auto mono = worst (1, "Wall of Sound"), stereo = worst (2, "Lofi Wobble");
        std::printf ("    all dry, against the input: %.1f dB from one channel, %.1f dB from two\n", mono, stereo);
        check (mono < -120.0, "one channel, all dry, comes out on both as it went in");
        check (stereo < -120.0, "and so do two");

        auto host = makeProcessor();
        juce::AudioProcessor::BusesLayout folded;
        folded.inputBuses.add (juce::AudioChannelSet::stereo());
        folded.outputBuses.add (juce::AudioChannelSet::mono());
        check (! host->checkBusesLayoutSupported (folded), "two channels are not folded into one");
        check (host->getLatencySamples() == 0, "there is no latency");
    }

    // Every stock preset, played: none of them blows up.
    {
        const auto loud = mdxtest::makeNoise (2.0, kSr, 0.9f);
        double highest = 0.0;

        for (const auto& name : processor->getStockPresets())
        {
            auto host = makeProcessor();
            host->loadPreset (name);
            host->setPlayConfigDetails (2, 2, kSr, kBlock);
            host->prepareToPlay (kSr, kBlock);

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;

            for (size_t at = 0; at + kBlock <= loud.size(); at += kBlock)
            {
                buffer.copyFrom (0, 0, loud.data() + at, kBlock);
                buffer.copyFrom (1, 0, loud.data() + at, kBlock);
                host->processBlock (buffer, midi);

                const auto peak = (double) buffer.getMagnitude (0, kBlock);
                highest = std::isfinite (peak) ? std::max (highest, peak) : 1.0e9;
            }
        }

        std::printf ("    the loudest any stock preset gets on noise at -1 dB: %.1f dB\n", 20.0 * std::log10 (highest + 1.0e-12));
        check (highest < 8.0, "no stock preset blows up");
    }

    // The types, read off the settings.
    {
        using P = onirique::ModBoxProcessor;

        check (processor->getPresetTypes ("Clean Flanger") == P::flanger, "a short delay against the dry sound is a flanger");
        check (processor->getPresetTypes ("Wall of Sound") == P::chorus, "a long one is a chorus");
        check (processor->getPresetTypes ("Pure Vibrato") == P::vibrato, "the voices alone are a vibrato");
        check (processor->getPresetTypes ("Alias Flange") == (P::flanger | P::bitCrusher), "a crushed flanger is both");
        check (processor->getPresetTypes ("Scorched Chorus") == (P::chorus | P::distortion), "and a distorted chorus is both");
        check (processor->getPresetTypes ("no such preset") == 0, "a preset that is not there has no type");

        int counts[5] = {};

        for (const auto& name : processor->getStockPresets())
            for (int bit = 0; bit < 5; ++bit)
                counts[bit] += (processor->getPresetTypes (name) >> bit) & 1;

        std::printf ("    stock presets: %d flanger, %d chorus, %d vibrato, %d bit crusher, %d distortion\n",
                     counts[0], counts[1], counts[2], counts[3], counts[4]);
        check (*std::min_element (counts, counts + 5) >= 4, "there are several stock presets of each type");
    }

    // Presets.
    check (processor->getStockPresets().size() >= 5, "there are stock presets");
    processor->loadPreset ("Lofi Wobble");
    check (processor->getPresetName() == "Lofi Wobble" && get ("circuit") > 99.0f && get ("stages") < 0.5f
               && get ("shape") > 0.5f && std::abs (get ("delay") - 18.0f) < 0.05f,
           "a stock preset sets what it names");
    processor->loadPreset ("Default");
    check (get ("circuit") < 0.5f && std::abs (get ("stages") - 2.0f) < 0.01f && get ("shape") < 0.5f
               && std::abs (get ("delay") - 4.0f) < 0.05f,
           "and Default puts everything back");

    const juce::String name ("zz selftest " + juce::String (juce::Time::currentTimeMillis()));
    set ("stereo", 133.0f);
    set ("voices", 5.0f);
    set ("keep", 240.0f);
    check (processor->saveUserPreset (name) == name && processor->getUserPresets().contains (name), "a user preset is saved under its name");
    check (processor->saveUserPreset ("Default") == "Default (user)", "a stock preset's name cannot be taken");
    processor->deleteUserPreset ("Default (user)");

    processor->loadPreset ("Default");
    processor->loadPreset (name);
    check (std::abs (get ("stereo") - 133.0f) < 0.01f && std::abs (get ("voices") - 5.0f) < 0.01f && std::abs (get ("keep") - 240.0f) < 0.5f,
           "and comes back as it was saved");

    processor->deleteUserPreset (name);
    check (! processor->getUserPresets().contains (name) && processor->getPresetName() == "Default", "a user preset can be deleted");
    processor->deleteUserPreset ("Wall of Sound");
    check (processor->getStockPresets().contains ("Wall of Sound"), "a stock preset cannot");

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const juce::ScopedJuceInitialiser_GUI gui;

    if (juce::String (argv[1]) == "--selftest")
        return selfTest();

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const auto preset = argc > 2 ? juce::String (argv[2]) : juce::String();
    const auto width = argc > 4 ? juce::String (argv[3]).getIntValue() : 1240;
    const auto height = argc > 4 ? juce::String (argv[4]).getIntValue() : 690;

    {
        Director director (file, preset, width, height);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return file.existsAsFile() ? 0 : 1;
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    const Identifier kPresetProperty ("preset");
    const Identifier kSlotProperty ("slot");
    const Identifier kOtherSlot ("OtherSlot");
    const Identifier kSnapshot ("Snapshot");
    const char* const kPresetExtension = ".modbox";

    /** What a preset or an A/B slot leaves as it is: how the plugin is being
        listened to, not what it does to the sound. */
    bool isStored (const String& id)
    {
        return id != "bypass";
    }

    /** A range that is even to the ear: each octave, or each doubling of a
        time, takes the same travel. */
    NormalisableRange<float> logRange (float low, float high)
    {
        return { low, high,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return jlimit (a, b, v); } };
    }

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    /** A value that can go either way says which way it went. */
    Attributes signedValue (int decimals, const String& unit)
    {
        return Attributes()
            .withStringFromValueFunction ([decimals, unit] (float v, int)
                                          {
                                              const auto text = String (v, decimals);
                                              return (v > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text + " " + unit;
                                          })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes hertz()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz"
                                                                  : v < 10.0f ? String (v, 2) + " Hz"
                                                                              : String (roundToInt (v)) + " Hz";
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              const auto v = parseNumber (t);
                                              return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                                          });
    }

    /** The keep frequency: at the foot of its range there is nothing kept. */
    Attributes keepFrequency()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? String ("Off") : String (roundToInt (v)) + " Hz"; })
            .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("o") ? 20.0f : parseNumber (t); });
    }

    Attributes milliseconds()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " ms"; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes percent()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes degrees()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + String::fromUTF8 (" °"); })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    /** The crusher's knob runs from nothing to the most, and says what that
        leaves: so many bits. A number typed with its unit is taken as that. */
    Attributes bits()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              return v <= 0.0f ? String ("Off") : String (mdx::crushBits (v * 0.01f), 1) + " bit";
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              if (t.containsIgnoreCase ("o"))
                                                  return 0.0f;

                                              const auto v = parseNumber (t);
                                              return t.containsIgnoreCase ("b") ? (16.0f - v) * 100.0f / 13.0f : v;
                                          });
    }

    /** And the downsampler's, the rate the voices are held at. */
    Attributes heldRate()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              if (v <= 0.0f)
                                                  return String ("Off");

                                              const auto hz = mdx::holdHz (v * 0.01f);
                                              return hz >= 1000.0f ? String (hz / 1000.0f, 1) + " kHz" : String (roundToInt (hz)) + " Hz";
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              if (t.containsIgnoreCase ("o"))
                                                  return 0.0f;

                                              const auto v = parseNumber (t);

                                              if (! t.containsIgnoreCase ("h"))
                                                  return v;

                                              const auto hz = jlimit (300.0f, 40000.0f, t.containsIgnoreCase ("k") ? v * 1000.0f : v);
                                              return 100.0f * std::log (hz / 40000.0f) / std::log (300.0f / 40000.0f);
                                          });
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;
        const mdx::Settings defaults;
        const NormalisableRange<float> hundred { 0.0f, 100.0f, 1.0f };

        auto number = [&layout] (const String& id, const String& name, NormalisableRange<float> range,
                                 float value, const Attributes& attributes)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, value, attributes));
        };

        // Circuit.
        number ("circuit", "Circuit", hundred, defaults.circuit * 100.0f, percent());
        number ("drive", "Drive", hundred, defaults.drive * 100.0f, percent());
        number ("drift", "Drift", hundred, defaults.drift * 100.0f, percent());
        number ("noise", "Noise", hundred, defaults.noise * 100.0f, percent());

        StringArray stages;

        for (int i = 0; i < mdx::kNumStageChoices; ++i)
            stages.add (String (mdx::stagesOf (i)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stages", 1 }, "BBD Stages", stages, defaults.stages));

        // Density.
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "voices", 1 }, "Voices", 1, mdx::kMaxVoices, defaults.voices));
        number ("spread", "Spread", hundred, defaults.spread * 100.0f, percent());
        number ("keep", "Bass Keep", logRange (20.0f, 500.0f), defaults.keepHz, keepFrequency());

        // Modulation.
        number ("rate", "Rate", logRange (0.01f, 20.0f), defaults.rateHz, hertz());
        number ("depth", "Depth", hundred, defaults.depth * 100.0f, percent());
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 }, "LFO Shape",
                                                            StringArray { "Sine", "Triangle" }, defaults.shape));
        number ("rate2", "LFO 2 Rate", logRange (0.01f, 20.0f), defaults.rate2Hz, hertz());
        number ("lfo2", "LFO 2", hundred, defaults.lfo2 * 100.0f, percent());
        number ("stereo", "Stereo Phase", { 0.0f, 180.0f, 1.0f }, defaults.stereoDeg, degrees());

        // Intensity.
        number ("delay", "Delay", logRange (mdx::kMinDelayMs, mdx::kMaxDelayMs), defaults.delayMs, milliseconds());
        number ("feedback", "Feedback", { -100.0f, 100.0f, 1.0f }, defaults.feedback * 100.0f, signedValue (0, "%"));
        number ("cross", "Cross Feedback", hundred, defaults.cross * 100.0f, percent());
        number ("mix", "Mix", hundred, defaults.mix * 100.0f, percent());

        // Texture.
        number ("crush", "Bit Crush", hundred, defaults.crush * 100.0f, bits());
        number ("downsample", "Downsample", hundred, defaults.downsample * 100.0f, heldRate());
        number ("distortion", "Distortion", hundred, defaults.distortion * 100.0f, percent());

        number ("out", "Output", { -24.0f, 24.0f, 0.1f }, defaults.outDb, signedValue (1, "dB"));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

        return layout;
    }

    //==============================================================================
    struct StockPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // whatever differs from the defaults
    };

    // Starting points, set by reasoning rather than by ear.
    const std::vector<StockPreset>& stockPresets()
    {
        static const std::vector<StockPreset> presets
        {
            { "Default", {} },

            { "Barely There", { { "delay", 7.0f }, { "depth", 12.0f }, { "rate", 0.2f }, { "feedback", 0.0f },
                                { "mix", 18.0f }, { "stereo", 120.0f } } },

            { "Clean Flanger", { { "delay", 2.5f }, { "depth", 70.0f }, { "rate", 0.25f }, { "feedback", 55.0f } } },

            { "Liquid", { { "delay", 3.0f }, { "depth", 65.0f }, { "rate", 0.4f }, { "feedback", -60.0f },
                          { "stereo", 140.0f } } },

            { "Jet Engine", { { "delay", 1.2f }, { "depth", 90.0f }, { "rate", 0.12f }, { "feedback", -85.0f },
                              { "stereo", 30.0f } } },

            { "Metal Tube", { { "delay", 6.0f }, { "depth", 25.0f }, { "rate", 0.08f }, { "feedback", 90.0f },
                              { "cross", 40.0f } } },

            { "Fast Vibe", { { "delay", 5.0f }, { "depth", 35.0f }, { "rate", 5.5f }, { "feedback", 0.0f },
                             { "mix", 100.0f }, { "stereo", 0.0f } } },

            { "Vintage Flanger", { { "circuit", 100.0f }, { "drive", 40.0f }, { "noise", 10.0f },
                                   { "delay", 3.0f }, { "depth", 80.0f }, { "rate", 0.18f }, { "shape", 1.0f },
                                   { "feedback", 70.0f } } },

            { "BBD Chorus", { { "circuit", 100.0f }, { "drive", 25.0f }, { "drift", 40.0f },
                              { "voices", 2.0f }, { "spread", 40.0f },
                              { "delay", 12.0f }, { "depth", 30.0f }, { "rate", 0.6f }, { "feedback", 0.0f },
                              { "mix", 45.0f }, { "stereo", 120.0f } } },

            { "Dimension", { { "circuit", 80.0f }, { "drive", 15.0f }, { "drift", 20.0f },
                             { "voices", 2.0f }, { "spread", 0.0f }, { "keep", 120.0f },
                             { "delay", 9.0f }, { "depth", 22.0f }, { "rate", 0.45f }, { "feedback", 0.0f },
                             { "mix", 40.0f }, { "stereo", 180.0f } } },

            { "Another Dimension", { { "circuit", 60.0f }, { "voices", 4.0f }, { "spread", 70.0f }, { "keep", 150.0f },
                                     { "delay", 14.0f }, { "depth", 35.0f }, { "rate", 0.3f },
                                     { "lfo2", 35.0f }, { "rate2", 0.07f },
                                     { "feedback", 15.0f }, { "cross", 100.0f }, { "mix", 55.0f }, { "stereo", 180.0f } } },

            { "Wall of Sound", { { "circuit", 70.0f }, { "drive", 45.0f }, { "drift", 60.0f },
                                 { "voices", 8.0f }, { "spread", 100.0f }, { "keep", 180.0f },
                                 { "delay", 22.0f }, { "depth", 45.0f }, { "rate", 0.5f },
                                 { "lfo2", 40.0f }, { "rate2", 0.17f },
                                 { "feedback", 25.0f }, { "cross", 50.0f }, { "mix", 65.0f }, { "stereo", 180.0f } } },

            { "Lofi Wobble", { { "circuit", 100.0f }, { "drive", 75.0f }, { "drift", 100.0f }, { "noise", 45.0f },
                               { "stages", 0.0f },
                               { "delay", 18.0f }, { "depth", 55.0f }, { "rate", 1.4f }, { "shape", 1.0f },
                               { "feedback", 30.0f }, { "mix", 60.0f } } },

            // Vibrato: the voices alone, so that the sweep is heard as pitch.
            { "Pure Vibrato", { { "delay", 4.0f }, { "depth", 30.0f }, { "rate", 5.0f }, { "feedback", 0.0f },
                                { "mix", 100.0f }, { "stereo", 0.0f } } },

            { "Tape Vibrato", { { "circuit", 100.0f }, { "drive", 35.0f }, { "drift", 80.0f }, { "noise", 8.0f },
                                { "delay", 8.0f }, { "depth", 28.0f }, { "rate", 3.2f }, { "feedback", 0.0f },
                                { "mix", 100.0f }, { "stereo", 20.0f } } },

            { "Seasick", { { "delay", 14.0f }, { "depth", 60.0f }, { "rate", 0.7f }, { "lfo2", 45.0f }, { "rate2", 0.23f },
                           { "feedback", 0.0f }, { "mix", 100.0f }, { "stereo", 60.0f } } },

            { "Rotary", { { "circuit", 50.0f }, { "drive", 30.0f }, { "voices", 2.0f }, { "spread", 30.0f },
                          { "delay", 3.0f }, { "depth", 45.0f }, { "rate", 6.5f }, { "feedback", 10.0f },
                          { "mix", 100.0f }, { "stereo", 180.0f }, { "keep", 140.0f } } },

            // Bit crusher: the voices on fewer bits, at a lower sample rate, or both.
            { "8-Bit Chorus", { { "voices", 3.0f }, { "spread", 60.0f }, { "delay", 13.0f }, { "depth", 30.0f },
                                { "rate", 0.55f }, { "feedback", 0.0f }, { "mix", 55.0f }, { "stereo", 150.0f },
                                { "crush", 62.0f } } },

            { "Alias Flange", { { "delay", 2.0f }, { "depth", 75.0f }, { "rate", 0.2f }, { "feedback", 65.0f },
                                { "downsample", 45.0f } } },

            { "Old Sampler", { { "circuit", 40.0f }, { "voices", 2.0f }, { "delay", 16.0f }, { "depth", 25.0f },
                               { "rate", 0.4f }, { "feedback", 0.0f }, { "mix", 60.0f },
                               { "crush", 31.0f }, { "downsample", 28.0f } } },

            { "Dial-Up", { { "delay", 1.5f }, { "depth", 85.0f }, { "rate", 0.9f }, { "shape", 1.0f },
                           { "feedback", -75.0f }, { "cross", 60.0f }, { "mix", 70.0f },
                           { "crush", 80.0f }, { "downsample", 68.0f } } },

            { "Crushed Vibrato", { { "delay", 6.0f }, { "depth", 35.0f }, { "rate", 4.2f }, { "feedback", 0.0f },
                                   { "mix", 100.0f }, { "stereo", 90.0f }, { "crush", 70.0f }, { "downsample", 35.0f } } },

            // Distortion: the voices driven into a curve.
            { "Fuzz Flanger", { { "delay", 2.2f }, { "depth", 80.0f }, { "rate", 0.15f }, { "feedback", 60.0f },
                                { "mix", 60.0f }, { "distortion", 65.0f }, { "keep", 110.0f } } },

            { "Scorched Chorus", { { "circuit", 80.0f }, { "drive", 60.0f }, { "voices", 4.0f }, { "spread", 80.0f },
                                   { "delay", 15.0f }, { "depth", 35.0f }, { "rate", 0.45f }, { "feedback", 10.0f },
                                   { "mix", 55.0f }, { "stereo", 180.0f }, { "distortion", 40.0f } } },

            { "Burning Jet", { { "delay", 1.0f }, { "depth", 95.0f }, { "rate", 0.09f }, { "feedback", -90.0f },
                               { "mix", 65.0f }, { "stereo", 40.0f }, { "distortion", 85.0f } } },

            { "Warm Edge", { { "delay", 9.0f }, { "depth", 20.0f }, { "rate", 0.3f }, { "voices", 2.0f },
                             { "feedback", 0.0f }, { "mix", 40.0f }, { "distortion", 22.0f } } },

            { "Broken Speaker", { { "circuit", 100.0f }, { "drive", 80.0f }, { "drift", 70.0f }, { "stages", 1.0f },
                                  { "delay", 11.0f }, { "depth", 40.0f }, { "rate", 2.4f }, { "feedback", 20.0f },
                                  { "mix", 100.0f }, { "distortion", 75.0f }, { "crush", 46.0f } } },
        };

        return presets;
    }

    const StockPreset* findStock (const String& name)
    {
        for (const auto& preset : stockPresets())
            if (name.equalsIgnoreCase (preset.name))
                return &preset;

        return nullptr;
    }

    File presetFile (const String& name)
    {
        return ModBoxProcessor::getUserPresetFolder()
                   .getChildFile (File::createLegalFileName (name) + kPresetExtension);
    }
}

/** The parameters' values, looked up once. */
struct ModBoxProcessor::Raw
{
    explicit Raw (AudioProcessorValueTreeState& state)
    {
        auto get = [&state] (const String& id) { return state.getRawParameterValue (id); };

        circuit = get ("circuit");   drive = get ("drive");       drift = get ("drift");
        noise = get ("noise");       stages = get ("stages");
        voices = get ("voices");     spread = get ("spread");     keep = get ("keep");
        rate = get ("rate");         depth = get ("depth");       shape = get ("shape");
        rate2 = get ("rate2");       lfo2 = get ("lfo2");         stereo = get ("stereo");
        delay = get ("delay");       feedback = get ("feedback"); cross = get ("cross");
        mix = get ("mix");           out = get ("out");           bypass = get ("bypass");
        crush = get ("crush");       downsample = get ("downsample");
        distortion = get ("distortion");
    }

    std::atomic<float> *circuit, *drive, *drift, *noise, *stages, *voices, *spread, *keep,
                       *rate, *depth, *shape, *rate2, *lfo2, *stereo,
                       *delay, *feedback, *cross, *mix, *out, *bypass,
                       *crush, *downsample, *distortion;
};

//==============================================================================
ModBoxProcessor::ModBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ModBox", createLayout()),
      raw (std::make_unique<Raw> (apvts))
{
    bypassParameter = apvts.getParameter ("bypass");

    for (auto& voice : voiceDelays)
        for (auto& delay : voice)
            delay.store (0.0f);

    for (auto& gain : voiceGains)
        gain.store (0.0f);
}

ModBoxProcessor::~ModBoxProcessor() = default;

bool ModBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;

    // One channel can be spread over two; two channels cannot be folded
    // into one.
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

mdx::Settings ModBoxProcessor::readSettings() const noexcept
{
    mdx::Settings s;
    const auto& r = *raw;

    s.circuit = r.circuit->load() * 0.01f;
    s.drive = r.drive->load() * 0.01f;
    s.drift = r.drift->load() * 0.01f;
    s.noise = r.noise->load() * 0.01f;
    s.stages = roundToInt (r.stages->load());

    s.voices = roundToInt (r.voices->load());
    s.spread = r.spread->load() * 0.01f;
    s.keepHz = r.keep->load();

    s.rateHz = r.rate->load();
    s.depth = r.depth->load() * 0.01f;
    s.shape = roundToInt (r.shape->load());
    s.rate2Hz = r.rate2->load();
    s.lfo2 = r.lfo2->load() * 0.01f;
    s.stereoDeg = r.stereo->load();

    s.delayMs = r.delay->load();
    s.feedback = r.feedback->load() * 0.01f;
    s.cross = r.cross->load() * 0.01f;
    s.mix = r.mix->load() * 0.01f;

    s.crush = r.crush->load() * 0.01f;
    s.downsample = r.downsample->load() * 0.01f;
    s.distortion = r.distortion->load() * 0.01f;

    s.outDb = r.out->load();
    s.bypass = r.bypass->load() > 0.5f;

    return s;
}

void ModBoxProcessor::prepareToPlay (double sampleRate, int)
{
    modulator.setSettings (readSettings());
    modulator.prepare (sampleRate, getMainBusNumInputChannels(), getMainBusNumOutputChannels());
}

void ModBoxProcessor::reset()
{
    modulator.reset();
}

void ModBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numIn = jmin (getMainBusNumInputChannels(), (int) mdx::kMaxChannels);
    const auto numOut = jmin (getMainBusNumOutputChannels(), (int) mdx::kMaxChannels);
    const auto numSamples = buffer.getNumSamples();

    if (numIn < 1 || numOut < 1 || buffer.getNumChannels() < jmax (numIn, numOut))
        return;

    // The one channel of a mono input is also the first of the two it comes
    // out on: the modulator reads each sample before it writes it.
    const float* in[mdx::kMaxChannels] = {};
    float* out[mdx::kMaxChannels] = {};

    for (int c = 0; c < numIn; ++c)
        in[c] = buffer.getReadPointer (c);

    for (int c = 0; c < numOut; ++c)
        out[c] = buffer.getWritePointer (c);

    modulator.setSettings (readSettings());
    modulator.process (in, out, numSamples);

    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    for (int k = 0; k < mdx::kMaxVoices; ++k)
    {
        voiceGains[k].store (modulator.voiceGain (k), std::memory_order_relaxed);

        for (int c = 0; c < mdx::kMaxChannels; ++c)
            voiceDelays[k][c].store (modulator.voiceDelayMs (k, c), std::memory_order_relaxed);
    }
}

AudioProcessorEditor* ModBoxProcessor::createEditor()
{
    return new ModBoxEditor (*this);
}

//==============================================================================
ValueTree ModBoxProcessor::capture() const
{
    ValueTree snapshot (kSnapshot);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter))
            if (isStored (ranged->paramID))
                snapshot.setProperty (ranged->paramID, ranged->convertFrom0to1 (ranged->getValue()), nullptr);

    return snapshot;
}

void ModBoxProcessor::apply (const ValueTree& snapshot)
{
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter);

        if (ranged == nullptr || ! isStored (ranged->paramID))
            continue;

        // What a snapshot does not mention goes back to its default, so that
        // an old preset still means the same thing after a parameter is added.
        const auto wanted = snapshot.hasProperty (ranged->paramID)
                                ? ranged->convertTo0to1 ((float) snapshot[ranged->paramID])
                                : ranged->getDefaultValue();

        if (std::abs (wanted - ranged->getValue()) > 1.0e-6f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (wanted);
            ranged->endChangeGesture();
        }
    }
}

void ModBoxProcessor::switchToSlot (int slot)
{
    slot = jlimit (0, 1, slot);

    if (slot == activeSlot)
        return;

    ValueTree wanted;

    {
        const ScopedLock lock (slotLock);
        slots[activeSlot] = capture();
        wanted = slots[slot];
        activeSlot = slot;
    }

    // A slot never visited starts out as a copy of the one just left.
    if (wanted.isValid())
        apply (wanted);
}

void ModBoxProcessor::copyToOtherSlot()
{
    const ScopedLock lock (slotLock);
    slots[1 - activeSlot] = capture();
}

//==============================================================================
File ModBoxProcessor::getUserPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
               .getChildFile ("Onirique").getChildFile ("ModBox").getChildFile ("Presets");
}

StringArray ModBoxProcessor::getStockPresets() const
{
    StringArray names;

    for (const auto& preset : stockPresets())
        names.add (preset.name);

    return names;
}

StringArray ModBoxProcessor::getUserPresets() const
{
    StringArray names;

    for (const auto& file : getUserPresetFolder().findChildFiles (File::findFiles, false,
                                                                  String ("*") + kPresetExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

String ModBoxProcessor::getPresetName() const
{
    return apvts.state.getProperty (kPresetProperty, "Default").toString();
}

void ModBoxProcessor::setPresetName (const String& name)
{
    apvts.state.setProperty (kPresetProperty, name, nullptr);
}

bool ModBoxProcessor::isUserPreset (const String& name) const
{
    return findStock (name) == nullptr && presetFile (name).existsAsFile();
}

void ModBoxProcessor::loadPreset (const String& name)
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, value] : stock->values)
            snapshot.setProperty (id, value, nullptr);

        apply (snapshot);
        setPresetName (stock->name);
        return;
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
        {
            apply (snapshot);
            setPresetName (name);
        }
    }
}

String ModBoxProcessor::saveUserPreset (const String& requested)
{
    auto name = File::createLegalFileName (requested.trim());

    if (name.isEmpty())
        return {};

    // The stock presets are not the user's to overwrite.
    if (findStock (name) != nullptr)
        name << " (user)";

    const auto file = presetFile (name);

    if (! file.getParentDirectory().createDirectory().wasOk())
        return {};

    if (const auto xml = capture().createXml())
    {
        if (xml->writeTo (file))
        {
            setPresetName (name);
            return name;
        }
    }

    return {};
}

void ModBoxProcessor::deleteUserPreset (const String& name)
{
    if (! isUserPreset (name))
        return;

    presetFile (name).deleteFile();

    // The settings stay as they are; they are just no longer anyone's.
    if (getPresetName() == name)
        setPresetName ("Default");
}

//==============================================================================
StringArray ModBoxProcessor::getPresetTypeNames()
{
    return { "Flanger", "Chorus", "Vibrato", "Bit Crusher", "Distortion" };
}

int ModBoxProcessor::typesOf (const ValueTree& snapshot) const
{
    // What a snapshot does not mention is at its default, as when it is loaded.
    auto value = [&] (const char* id)
    {
        if (snapshot.hasProperty (id))
            return (float) snapshot[id];

        auto* parameter = apvts.getParameter (id);
        return parameter->convertFrom0to1 (parameter->getDefaultValue());
    };

    int types = 0;

    // The voices alone, with next to nothing fed back, are heard as a pitch
    // that moves. Against the dry sound they are a comb: a flanger while
    // the delay is short, a chorus once it is long enough to be heard as
    // a second voice.
    if (value ("mix") >= 95.0f && std::abs (value ("feedback")) <= 20.0f && value ("depth") > 0.0f)
        types |= vibrato;
    else if (value ("delay") < 8.0f)
        types |= flanger;
    else
        types |= chorus;

    if (value ("crush") > 0.0f || value ("downsample") > 0.0f)
        types |= bitCrusher;

    if (value ("distortion") > 0.0f)
        types |= distortion;

    return types;
}

int ModBoxProcessor::getPresetTypes (const String& name) const
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, value] : stock->values)
            snapshot.setProperty (id, value, nullptr);

        return typesOf (snapshot);
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
            return typesOf (snapshot);
    }

    return 0;
}

StringArray ModBoxProcessor::getFilteredPresets() const
{
    StringArray names;

    auto add = [&] (const StringArray& from)
    {
        for (const auto& name : from)
            if (typeFilter == 0 || (getPresetTypes (name) & typeFilter) != 0)
                names.add (name);
    };

    if (sourceFilter != userOnly)
        add (getStockPresets());

    if (sourceFilter != stockOnly)
        add (getUserPresets());

    return names;
}

void ModBoxProcessor::restoreSettings (const ValueTree& settings, const String& presetName)
{
    apply (settings);
    setPresetName (presetName);
}

//==============================================================================
void ModBoxProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();

    {
        const ScopedLock lock (slotLock);

        state.setProperty (kSlotProperty, activeSlot, nullptr);
        state.removeChild (state.getChildWithName (kOtherSlot), nullptr);

        if (slots[1 - activeSlot].isValid())
        {
            ValueTree other (kOtherSlot);
            other.copyPropertiesFrom (slots[1 - activeSlot], nullptr);
            state.appendChild (other, nullptr);
        }
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void ModBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = ValueTree::fromXml (*xml);
    const auto other = state.getChildWithName (kOtherSlot);

    {
        const ScopedLock lock (slotLock);

        activeSlot = jlimit (0, 1, (int) state.getProperty (kSlotProperty, 0));
        slots[activeSlot] = {};
        slots[1 - activeSlot] = {};

        if (other.isValid())
        {
            ValueTree snapshot (kSnapshot);
            snapshot.copyPropertiesFrom (other, nullptr);
            slots[1 - activeSlot] = snapshot;
        }
    }

    state.removeChild (other, nullptr);
    apvts.replaceState (state);
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::ModBoxProcessor();
}

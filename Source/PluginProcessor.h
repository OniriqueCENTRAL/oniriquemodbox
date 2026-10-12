// Onirique ModBox — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseGuard.h"
#include "dsp/Modulator.h"

namespace onirique
{

using namespace juce;

/** The plugin instance: parameters in, a block through the modulator, and
    where each voice is in its sweep for the editor to draw. It also keeps
    what is not a parameter: the two settings being compared, and the presets. */
class ModBoxProcessor final : public AudioProcessor
{
public:
    ModBoxProcessor();
    ~ModBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }

    // What the feedback leaves ringing once the input has stopped.
    double getTailLengthSeconds() const override { return 2.0; }

    AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The settings as the parameters stand now. */
    mdx::Settings readSettings() const noexcept;

    /** Where a voice is reading the line just now, in ms, and how much of
        it is heard: 0 for a voice that is not in use. */
    float getVoiceDelayMs (int voice, int channel) const noexcept { return voiceDelays[voice][channel].load (std::memory_order_relaxed); }
    float getVoiceGain (int voice) const noexcept { return voiceGains[voice].load (std::memory_order_relaxed); }

    /** The rate the response is to be drawn for. */
    double getDisplaySampleRate() const noexcept { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }

    //==============================================================================
    // A and B: two settings held side by side. Everything below is for the
    // message thread.
    int getActiveSlot() const noexcept { return activeSlot; }

    /** Puts the current settings away and brings the other ones back. */
    void switchToSlot (int slot);

    /** Makes the other slot the same as the one being heard. */
    void copyToOtherSlot();

    //==============================================================================
    // Presets. The stock ones are built in; the user's are files, one each.
    StringArray getStockPresets() const;
    StringArray getUserPresets() const;
    String getPresetName() const;
    bool isUserPreset (const String& name) const;

    void loadPreset (const String& name);

    /** Writes the current settings under a name, and returns the name it was
        given: a stock preset's name cannot be taken. */
    String saveUserPreset (const String& name);
    void deleteUserPreset (const String& name);

    static File getUserPresetFolder();

    //==============================================================================
    // What kind of effect a preset is. Nobody labels them: it is read off
    // the settings, so the user's own presets are sorted like the stock ones.
    enum PresetType { flanger = 1, chorus = 2, vibrato = 4, bitCrusher = 8, distortion = 16 };

    /** The names of the types, in the order of their bits. */
    static StringArray getPresetTypeNames();

    /** The types of a preset, as bits of PresetType; 0 if there is no such preset. */
    int getPresetTypes (const String& name) const;

    /** What the preset browser is narrowed to, kept here so that it outlives
        the editor: any of the types chosen (0 for no choice, which is all
        of them), and where a preset comes from. */
    enum PresetSource { anySource = 0, stockOnly = 1, userOnly = 2 };

    int getTypeFilter() const noexcept { return typeFilter; }
    void setTypeFilter (int types) noexcept { typeFilter = types; }
    int getSourceFilter() const noexcept { return sourceFilter; }
    void setSourceFilter (int source) noexcept { sourceFilter = source; }

    /** The presets the browser is narrowed to, the stock ones first. */
    StringArray getFilteredPresets() const;

    /** The settings as they stand, to be put back later with the name they
        went by: what the browser does when it is cancelled. */
    ValueTree getSettings() const { return capture(); }
    void restoreSettings (const ValueTree& settings, const String& presetName);

    AudioProcessorValueTreeState apvts;

    /** The licence ONIRIQUE CENTRAL wrote on this machine for this plugin, under
        the identifier the plugin has in its catalogue. */
    onirique::license::Guard license { "onirique-modbox" };

private:
    /** The values of everything a preset or a slot holds, by parameter. */
    ValueTree capture() const;
    void apply (const ValueTree&);
    void setPresetName (const String&);
    int typesOf (const ValueTree& snapshot) const;

    mdx::Modulator modulator;

    struct Raw;
    std::unique_ptr<Raw> raw;
    AudioProcessorParameter* bypassParameter = nullptr;

    std::atomic<float> voiceDelays[mdx::kMaxVoices][mdx::kMaxChannels];
    std::atomic<float> voiceGains[mdx::kMaxVoices];

    CriticalSection slotLock;
    ValueTree slots[2];
    int activeSlot = 0;
    int typeFilter = 0, sourceFilter = anySource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModBoxProcessor)
};

} // namespace onirique

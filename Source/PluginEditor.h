// Onirique ModBox — editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/PresetBrowser.h"
#include "ui/SweepDisplay.h"
#include "ui/Theme.h"

namespace onirique
{

/** Three zones, each with one job: the header is which settings are loaded,
    the sweep shows what they do to the sound just now, and under it the
    five families of controls stand side by side: what the line is made of,
    how thick the sound is, how it moves, how much of it there is, and how
    rough it comes out. */
class ModBoxEditor final : public AudioProcessorEditor,
                           private Timer
{
public:
    explicit ModBoxEditor (ModBoxProcessor&);
    ~ModBoxEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    /** Turns the preset's name into the field its new name is typed in. */
    void beginSaving();

    /** Opens the preset browser, as a click on the preset's name does. */
    void showBrowser() { browser.open(); }
    void closeBrowser (bool keepPreset) { browser.close (keepPreset); }
    bool isBrowserOpen() const { return browser.isVisible(); }

    /** What is clicked to open it. */
    Component& getPresetField() noexcept { return presetBox; }

private:
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

    static constexpr int kNumPanels = 5;

    void timerCallback() override;
    void paintHeader (Graphics&, Rectangle<int> header);
    int titleRight() const;

    /** Where a control of a family goes: its row, and its place in it. */
    Rectangle<int> cell (int panel, int row, int column) const;

    /** Fills the preset menu from what is built in and what is on disk. */
    void refreshPresets();
    void stepPreset (int direction);
    void deleteClicked();

    /** The name of the preset, drawn as a menu is; but what it opens is the
        browser, not a list of its own. */
    struct PresetField final : ComboBox
    {
        void showPopup() override
        {
            // A menu counts as open from the click until it is hidden, and
            // takes no other click meanwhile: there being no menu, it is
            // hidden at once.
            hidePopup();

            if (onOpen != nullptr)
                onOpen();
        }

        std::function<void()> onOpen;
    };

    ModBoxProcessor& processor;

    SweepDisplay sweep;

    // The header: presets, the two slots, and bypass.
    TextButton previousButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };
    PresetField presetBox;
    PresetBrowser browser;
    Label presetNameEditor;
    StringArray presetNames;
    int deleteArmed = 0;    // ticks left in which a second click deletes

    TextButton slotAButton { "A" }, slotBButton { "B" }, copyButton;
    TextButton bypassButton { "BYPASS" }, autoGainButton { "AUTO GAIN" };

    // Circuit: from digital to analog, and what analog brings with it.
    Knob circuit { "CIRCUIT" }, drive { "DRIVE" }, drift { "DRIFT" }, noise { "NOISE" };

    // Density: how many voices, how far apart, on how many stages, and
    // what of the bass is kept out of it.
    Knob voices { "VOICES" }, spread { "SPREAD" }, stages { "STAGES" }, keep { "BASS KEEP" };

    // Modulation: two LFOs, and the right one ahead of the left.
    Knob rate { "RATE" }, depth { "DEPTH" }, stereo { "STEREO" }, rate2 { "RATE 2" }, lfo2 { "LFO 2" };
    TextButton sineButton { "SINE" }, triangleButton { "TRI" };
    std::unique_ptr<ParameterAttachment> shapeAttachment;

    // Intensity.
    Knob delay { "DELAY" }, feedback { "FEEDBACK" }, cross { "CROSS" }, mix { "MIX" };

    // Texture: what roughens the voices once they have been read.
    Knob crush { "CRUSH" }, downsample { "DOWNSAMPLE" }, distortion { "DISTORTION" };

    // The foot.
    ValueField output { "OUT" };

    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    // Where the families are, set by resized().
    Rectangle<int> panels[kNumPanels], shapeHeading;

    //==============================================================================
    /** The description of whatever the pointer is resting on.

        Shown only once the pointer has been still on a control for two full
        seconds. From then on it stays for as long as the pointer is on that
        control, moving or not, and goes when it leaves. */
    struct HoverTip final : Component
    {
        void paint (Graphics& g) override
        {
            getLookAndFeel().drawTooltip (g, text, getWidth(), getHeight());
        }

        String text;
    };

    struct Described
    {
        Component* component;
        String text;
    };

    void describe (std::initializer_list<Component*>, const char* text);
    void updateHoverTip();

    std::vector<Described> described;
    HoverTip hoverTip;
    int resting = -1;           // which of the described, or none
    Point<int> restingAt;
    uint32 restingSince = 0;

    /** The sigil with its ember glow already baked in, at twice the size it is
        drawn so it stays sharp on a scaled display. */
    Image logo;

    /** When the editor opened: the light that runs along the foot of the
        header, and the stars beside the name, keep time from here. */
    double openedAt = Time::getMillisecondCounterHiRes();

    /** The licence badge and its activation page. Declared last and added
        last: it lies over everything else. */
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModBoxEditor)
};

} // namespace onirique

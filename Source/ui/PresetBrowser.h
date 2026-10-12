// Onirique ModBox — the preset browser: filters on the left, presets in columns
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

namespace onirique
{

/** Opened from the preset's name, over everything under the header.

    On the left, what the list is narrowed to: where a preset comes from,
    and the kinds of effect wanted, any number of them. On the right, the
    presets that are left, in columns, with a field to find one by name.

    A click loads a preset, to be heard there and then. OK, or a double
    click, keeps it; Cancel, or Escape, puts back what was there when the
    browser was opened. The filters stay as they were left, and are what
    the two arrows of the header step through. */
class PresetBrowser final : public Component
{
public:
    explicit PresetBrowser (ModBoxProcessor&);

    void open();
    void close (bool keep);

    /** Called once the browser has closed, either way. */
    std::function<void()> onClose;

    void paint (Graphics&) override;
    void resized() override;
    bool keyPressed (const KeyPress&) override;
    void mouseDown (const MouseEvent&) override {}      // nothing under it is to be reached

private:
    /** The presets themselves, as many columns as it takes. */
    struct List final : Component
    {
        explicit List (PresetBrowser& b) : browser (b) {}

        void paint (Graphics&) override;
        void mouseMove (const MouseEvent&) override;
        void mouseExit (const MouseEvent&) override;
        void mouseDown (const MouseEvent&) override;

        int indexAt (Point<int>) const;
        void layout (int height);

        PresetBrowser& browser;
        StringArray names;
        Array<bool> user;
        int rows = 1, hovered = -1;
    };

    void rebuild();

    ModBoxProcessor& processor;

    TextButton sourceButtons[3];
    TextButton typeButtons[5];
    TextEditor search;
    TextButton cancelButton { "CANCEL" }, okButton { "OK" };

    Viewport viewport;
    List list { *this };

    Rectangle<int> side, sourceHeading, typeHeading;

    // What was there when the browser was opened.
    ValueTree settingsBefore;
    String nameBefore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace onirique

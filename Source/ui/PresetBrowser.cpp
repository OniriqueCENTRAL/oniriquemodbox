#include "PresetBrowser.h"

#include "Theme.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr int kSideWidth = 214;
    constexpr int kRowHeight = 24;
    constexpr int kColumnWidth = 196;
    constexpr int kChipHeight = 26;

    /** Lays buttons out as words on lines: each as wide as its text asks,
        on to the next line when the one it is on is full. Returns the
        bottom of the last line. */
    int flow (Rectangle<int> area, std::initializer_list<TextButton*> buttons)
    {
        const auto font = capsFont (10.0f);
        auto x = area.getX(), y = area.getY();

        for (auto* button : buttons)
        {
            const auto width = roundToInt (GlyphArrangement::getStringWidth (font, button->getButtonText())) + 26;

            if (x > area.getX() && x + width > area.getRight())
            {
                x = area.getX();
                y += kChipHeight + 6;
            }

            button->setBounds (x, y, width, kChipHeight);
            x += width + 6;
        }

        return y + kChipHeight;
    }
}

PresetBrowser::PresetBrowser (ModBoxProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);

    // Where a preset comes from: one of the three, always.
    const char* const sources[] = { "ALL", "FACTORY", "USER" };

    for (int i = 0; i < 3; ++i)
    {
        auto& button = sourceButtons[i];

        button.setButtonText (sources[i]);
        button.getProperties().set ("plain", true);
        button.onClick = [this, i]
        {
            processor.setSourceFilter (i);
            rebuild();
        };
        addAndMakeVisible (button);
    }

    // The kinds of effect: any number of them, and none is all of them.
    const auto types = ModBoxProcessor::getPresetTypeNames();

    for (int i = 0; i < 5; ++i)
    {
        auto& button = typeButtons[i];

        button.setButtonText (types[i].toUpperCase());
        button.getProperties().set ("plain", true);
        button.onClick = [this, i]
        {
            processor.setTypeFilter (processor.getTypeFilter() ^ (1 << i));
            rebuild();
        };
        addAndMakeVisible (button);
    }

    search.setTextToShowWhenEmpty ("Find preset...", haze);
    search.setFont (displayFont (14.0f, false));
    search.setIndents (10, 0);
    search.setJustification (Justification::centredLeft);
    search.setColour (TextEditor::backgroundColourId, Colour (0x73000000));
    search.setColour (TextEditor::outlineColourId, Colour (0x33ffd2d6));
    search.setColour (TextEditor::focusedOutlineColourId, ember.withAlpha (0.7f));
    search.onTextChange = [this] { rebuild(); };
    search.onEscapeKey = [this] { close (false); };
    search.onReturnKey = [this] { close (true); };
    addAndMakeVisible (search);

    for (auto* button : { &cancelButton, &okButton })
    {
        button->getProperties().set ("plain", true);
        addAndMakeVisible (*button);
    }

    cancelButton.onClick = [this] { close (false); };
    okButton.onClick = [this] { close (true); };

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void PresetBrowser::open()
{
    settingsBefore = processor.getSettings();
    nameBefore = processor.getPresetName();

    search.setText ({}, dontSendNotification);
    rebuild();

    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
}

void PresetBrowser::close (bool keep)
{
    if (! isVisible())
        return;

    if (! keep && settingsBefore.isValid())
        processor.restoreSettings (settingsBefore, nameBefore);

    setVisible (false);

    if (onClose != nullptr)
        onClose();
}

bool PresetBrowser::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey)
    {
        close (false);
        return true;
    }

    if (key == KeyPress::returnKey)
    {
        close (true);
        return true;
    }

    return false;
}

void PresetBrowser::rebuild()
{
    const auto source = processor.getSourceFilter();
    const auto types = processor.getTypeFilter();

    for (int i = 0; i < 3; ++i)
        sourceButtons[i].setToggleState (i == source, dontSendNotification);

    for (int i = 0; i < 5; ++i)
        typeButtons[i].setToggleState ((types & (1 << i)) != 0, dontSendNotification);

    const auto wanted = search.getText().trim();

    list.names.clear();
    list.user.clear();
    list.hovered = -1;

    for (const auto& name : processor.getFilteredPresets())
        if (wanted.isEmpty() || name.containsIgnoreCase (wanted))
        {
            list.names.add (name);
            list.user.add (processor.isUserPreset (name));
        }

    list.layout (viewport.getMaximumVisibleHeight());
    repaint();
}

void PresetBrowser::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (night);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Colour (0x40ffd2d6));
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    // The filters on their own ground, as the families of controls are.
    g.setColour (deep);
    g.fillRoundedRectangle (side.toFloat(), 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (side.toFloat(), 8.0f, 1.0f);

    g.setFont (capsFont (9.5f).withExtraKerningFactor (0.3f));
    g.setColour (haze);
    g.drawText ("SOURCE", sourceHeading, Justification::centredLeft, false);
    g.drawText ("TYPE", typeHeading, Justification::centredLeft, false);

    // With no type chosen, every type is.
    if (processor.getTypeFilter() == 0)
    {
        g.setColour (emberLit);
        g.drawText ("ANY", typeHeading, Justification::centredRight, false);
    }

    if (list.names.isEmpty())
    {
        g.setColour (haze);
        g.setFont (displayFont (14.0f, false));
        g.drawText (String::fromUTF8 ("Aucun preset ne correspond à ces filtres."), viewport.getBounds(), Justification::centred, false);
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (14);

    side = area.removeFromLeft (kSideWidth);
    area.removeFromLeft (14);

    auto filters = side.reduced (12, 12);

    sourceHeading = filters.removeFromTop (16);
    filters.removeFromTop (6);
    filters.setTop (flow (filters, { &sourceButtons[0], &sourceButtons[1], &sourceButtons[2] }) + 18);

    typeHeading = filters.removeFromTop (16);
    filters.removeFromTop (6);
    flow (filters, { &typeButtons[0], &typeButtons[1], &typeButtons[2], &typeButtons[3], &typeButtons[4] });

    auto foot = area.removeFromBottom (30);
    area.removeFromBottom (10);

    okButton.setBounds (foot.removeFromRight (70));
    foot.removeFromRight (6);
    cancelButton.setBounds (foot.removeFromRight (86));
    search.setBounds (foot.removeFromLeft (jmin (300, foot.getWidth() - 20)));

    viewport.setBounds (area);
    list.layout (viewport.getMaximumVisibleHeight());
}

//==============================================================================
void PresetBrowser::List::layout (int height)
{
    // Down each column, then on to the next: as many rows as there is room
    // for, less the scroll bar's when there is one.
    rows = jmax (1, (height - 10) / kRowHeight);

    const auto columns = jmax (1, (names.size() + rows - 1) / rows);

    setSize (columns * kColumnWidth, jmax (1, height - 10));
    repaint();
}

int PresetBrowser::List::indexAt (Point<int> at) const
{
    if (at.x < 0 || at.y < 0 || at.y >= rows * kRowHeight)
        return -1;

    const auto index = (at.x / kColumnWidth) * rows + at.y / kRowHeight;
    return isPositiveAndBelow (index, names.size()) ? index : -1;
}

void PresetBrowser::List::paint (Graphics& g)
{
    const auto loaded = browser.processor.getPresetName();
    const auto columns = (names.size() + rows - 1) / rows;

    g.setColour (line);

    for (int c = 1; c < columns; ++c)
        g.fillRect (c * kColumnWidth, 0, 1, rows * kRowHeight);

    for (int i = 0; i < names.size(); ++i)
    {
        const Rectangle<int> row ((i / rows) * kColumnWidth + 4, (i % rows) * kRowHeight, kColumnWidth - 8, kRowHeight);
        const auto cell = row.toFloat().reduced (0.0f, 1.5f);
        const auto current = names[i] == loaded;

        if (current)
        {
            g.setGradientFill (ColourGradient (Colour (0xff6e0f1a), 0.0f, cell.getY(), Colour (0xff3a060c), 0.0f, cell.getBottom(), false));
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            g.setColour (emberLit);
            g.drawRoundedRectangle (cell, cell.getHeight() * 0.5f, 1.2f);
        }
        else if (i == hovered)
        {
            g.setColour (Colour (0x26ffd2d6));
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
        }

        auto text = row.reduced (10, 0);

        // The user's own are marked as such.
        if (user[i])
        {
            g.setColour (current ? bone : haze);
            g.setFont (capsFont (8.0f));
            g.drawText ("USER", text.removeFromRight (32), Justification::centredRight, false);
        }

        g.setColour (current ? Colours::white : (i == hovered ? mist : bone.withAlpha (0.85f)));
        g.setFont (displayFont (14.0f, current));
        g.drawText (names[i], text, Justification::centredLeft, true);
    }
}

void PresetBrowser::List::mouseMove (const MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void PresetBrowser::List::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void PresetBrowser::List::mouseDown (const MouseEvent& e)
{
    // A click is heard at once: the preset is loaded, and the browser stays.
    // The second click of two chooses it, and the browser closes.
    const auto index = indexAt (e.getPosition());

    if (index < 0)
        return;

    if (e.getNumberOfClicks() >= 2)
    {
        browser.close (true);
        return;
    }

    browser.processor.loadPreset (names[index]);
    repaint();
}

} // namespace onirique

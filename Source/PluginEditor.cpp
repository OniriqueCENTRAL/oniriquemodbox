#include "PluginEditor.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    OniriqueLookAndFeel& sharedLookAndFeel()
    {
        static OniriqueLookAndFeel laf;
        return laf;
    }

    constexpr int kHeaderHeight = 68;
    constexpr int kTitleLeft = 72;
    constexpr int kFootHeight = 46;
    constexpr int kPanelGap = 12;
    constexpr int kPanelHeading = 26;

    // How many controls wide each family is.
    constexpr int kPanelColumns[] = { 2, 2, 3, 2, 2 };
    constexpr int kColumns = 11;
    const char* const kPanelNames[] = { "CIRCUIT", "DENSITY", "MODULATION", "INTENSITY", "TEXTURE" };

    Font titleFont (bool heavy)
    {
        return displayFont (23.0f, heavy).withExtraKerningFactor (0.09f);
    }

    /** A four-pointed star, as on the Onirique sigil. */
    Path star (Point<float> centre, float radius)
    {
        Path p;
        const auto waist = radius * 0.16f;

        p.startNewSubPath (centre.x, centre.y - radius);
        p.lineTo (centre.x + waist, centre.y - waist);
        p.lineTo (centre.x + radius, centre.y);
        p.lineTo (centre.x + waist, centre.y + waist);
        p.lineTo (centre.x, centre.y + radius);
        p.lineTo (centre.x - waist, centre.y + waist);
        p.lineTo (centre.x - radius, centre.y);
        p.lineTo (centre.x - waist, centre.y - waist);
        p.closeSubPath();
        return p;
    }

    constexpr int kLogoWidth = 44;
    constexpr int kLogoMargin = 8;   // room for the glow around it

    Image makeLogo()
    {
        const auto sigil = ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        if (! sigil.isValid())
            return {};

        const auto width = 2 * kLogoWidth;
        const auto height = width * sigil.getHeight() / sigil.getWidth();
        const auto margin = 2 * kLogoMargin;

        Image padded (Image::ARGB, width + 2 * margin, height + 2 * margin, true);

        {
            Graphics g (padded);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<int> (margin, margin, width, height).toFloat());
        }

        Image result (Image::ARGB, padded.getWidth(), padded.getHeight(), true);
        Graphics g (result);

        GlowEffect glow;
        glow.setGlowProperties (7.0f, ember.withAlpha (0.8f));
        glow.applyEffect (padded, g, 1.0f, 1.0f);

        return result;
    }
}

ModBoxEditor::ModBoxEditor (ModBoxProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sweep (p),
      browser (p)
{
    setLookAndFeel (&sharedLookAndFeel());
    logo = makeLogo();

    auto& state = processor.apvts;

    addAndMakeVisible (sweep);

    // Presets.
    for (auto* b : { &previousButton, &nextButton, &saveButton, &deleteButton,
                     &slotAButton, &slotBButton, &copyButton, &sineButton, &triangleButton })
    {
        b->getProperties().set ("plain", true);
        addAndMakeVisible (*b);
    }

    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { beginSaving(); };
    deleteButton.onClick = [this] { deleteClicked(); };

    // The preset's name is what opens the browser, where one is chosen.
    presetBox.onOpen = [this] { browser.open(); };
    addAndMakeVisible (presetBox);

    browser.onClose = [this] { refreshPresets(); };
    addChildComponent (browser);

    // The name of a preset being saved is typed where its name is shown.
    // It lies over the menu and looks like it, with the text where the menu
    // had it, so that the name seems to be typed into the menu itself.
    presetNameEditor.getProperties().set ("box", true);
    presetNameEditor.setJustificationType (Justification::centredLeft);
    presetNameEditor.setBorderSize ({ 0, 13, 0, 30 });
    presetNameEditor.setEditable (true);
    presetNameEditor.onTextChange = [this]
    {
        const auto saved = processor.saveUserPreset (presetNameEditor.getText());

        if (saved.isNotEmpty())
            refreshPresets();
    };
    presetNameEditor.onEditorHide = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);

    refreshPresets();

    // A and B.
    slotAButton.onClick = [this] { processor.switchToSlot (0); };
    slotBButton.onClick = [this] { processor.switchToSlot (1); };
    copyButton.onClick = [this] { processor.copyToOtherSlot(); };

    const std::pair<Knob*, const char*> knobs[] = { { &circuit, "circuit" }, { &drive, "drive" }, { &drift, "drift" }, { &noise, "noise" },
                                                    { &voices, "voices" }, { &spread, "spread" }, { &stages, "stages" }, { &keep, "keep" },
                                                    { &rate, "rate" }, { &depth, "depth" }, { &stereo, "stereo" },
                                                    { &rate2, "rate2" }, { &lfo2, "lfo2" },
                                                    { &delay, "delay" }, { &feedback, "feedback" }, { &cross, "cross" }, { &mix, "mix" },
                                                    { &crush, "crush" }, { &downsample, "downsample" }, { &distortion, "distortion" } };

    for (const auto& [knob, id] : knobs)
    {
        knob->attach (state, id);
        addAndMakeVisible (*knob);
    }

    circuit.setEnds ("DIG", "ANA");
    feedback.setEnds ("-", "+");
    stereo.setEnds ("0", "180");
    voices.setEnds ("1", String (mdx::kMaxVoices));

    bypassButton.setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "bypass", bypassButton));
    addAndMakeVisible (bypassButton);

    autoGainButton.setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "autogain", autoGainButton));
    addAndMakeVisible (autoGainButton);

    // Sine and triangle are the two halves of one switch.
    shapeAttachment = std::make_unique<ParameterAttachment> (*state.getParameter ("shape"), [this] (float value)
    {
        sineButton.setToggleState (value < 0.5f, dontSendNotification);
        triangleButton.setToggleState (value >= 0.5f, dontSendNotification);
    });

    sineButton.onClick = [this] { shapeAttachment->setValueAsCompleteGesture (0.0f); };
    triangleButton.onClick = [this] { shapeAttachment->setValueAsCompleteGesture (1.0f); };
    shapeAttachment->sendInitialUpdate();

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, "out", output));
    addAndMakeVisible (output);

    // What each control does, for a pointer that rests on it.
    describe ({ &presetBox }, "Le preset chargé. Un clic ouvre le navigateur : les presets en colonnes, à filtrer par origine et par type d'effet, ou à chercher par nom.");
    describe ({ &previousButton, &nextButton }, "Le preset d'avant, le preset d'après, parmi ceux que les filtres du navigateur laissent passer.");
    describe ({ &crush }, "Crush : réduit le nombre de bits des voix, de 16 à 3. Tout en bas : Off.");
    describe ({ &downsample }, "Downsample : abaisse la fréquence d'échantillonnage des voix, de 40 kHz à 300 Hz, avec le repliement qui va avec. Tout en bas : Off.");
    describe ({ &distortion }, "Distortion : pousse les voix dans une saturation franche, quel que soit le circuit. Le son direct reste propre.");
    describe ({ &saveButton }, "Enregistre les réglages comme un preset à toi : tape son nom, puis Entrée.");
    describe ({ &deleteButton }, "Supprime le preset affiché, si c'est l'un des tiens. Il demande une confirmation.");
    describe ({ &slotAButton, &slotBButton }, "A et B : deux réglages côte à côte, pour comparer.");
    describe ({ &copyButton }, "Copie les réglages qu'on écoute dans l'autre slot.");
    describe ({ &bypassButton }, "Bypass : l'entrée telle qu'elle est arrivée.");
    describe ({ &autoGainButton }, "Auto Gain : ramène la sortie au niveau de l'entrée, jusqu'à 12 dB dans un sens ou dans l'autre, pour comparer et régler sans être trompé par le volume. Le réglage Out s'ajoute par-dessus.");
    describe ({ &sweep }, "En haut, ce que le plugin fait au spectre à cet instant, côté gauche et côté droit. En bas, le retard de chaque voix, la gauche au-dessus de la droite.");

    describe ({ &circuit }, "Circuit : de la ligne à retard numérique parfaite (à 0) au circuit analogique BBD (à 100). À 0, ni saturation, ni filtre, ni dérive, ni souffle.");
    describe ({ &drive }, "Drive : la saturation douce à l'entrée de la ligne et dans la boucle de feedback. Compte d'autant plus que Circuit est haut.");
    describe ({ &drift }, "Drift : les petites variations de vitesse de l'horloge, lentes et rapides, propres à chaque voix. Compte d'autant plus que Circuit est haut.");
    describe ({ &noise }, "Noise : le souffle du circuit. À 0, il n'y en a pas.");
    describe ({ &voices }, "Voices : le nombre de voix empilées, de 1 (fin) à 8 (mur de son). Leurs LFO sont répartis sur le cycle.");
    describe ({ &spread }, "Spread : l'écart entre les retards des voix. À 0, elles sont toutes au même retard.");
    describe ({ &stages }, "Stages : le nombre d'étages du circuit BBD. Moins il y en a, plus le filtre se ferme tôt quand le retard s'allonge : plus sombre, plus lofi. Sans effet en numérique.");
    describe ({ &keep }, "Bass Keep : en dessous de cette fréquence, le son passe sans être traité, pour garder le punch. Tout en bas : Off.");
    describe ({ &rate }, "Rate : la vitesse du LFO principal.");
    describe ({ &depth }, "Depth : l'amplitude du balayage autour du retard choisi, de l'imperceptible à l'extrême.");
    describe ({ &stereo }, "Stereo : le décalage du LFO de droite par rapport à celui de gauche. À 0, l'effet est mono et centré ; à 180, les deux côtés balaient en sens opposés.");
    describe ({ &rate2 }, "Rate 2 : la vitesse du second LFO.");
    describe ({ &lfo2 }, "LFO 2 : la part du balayage confiée au second LFO. À 0, il ne fait rien ; au milieu, les deux se mêlent et le mouvement ne se répète plus.");
    describe ({ &sineButton, &triangleButton }, "La forme des LFO : sinus, ou triangle.");
    describe ({ &delay }, "Delay : le retard au centre du balayage. Court pour un flanger, plus long pour un chorus.");
    describe ({ &feedback }, "Feedback : ce qui est renvoyé dans la ligne. En positif, un son métallique et tranchant ; en négatif, la phase est inversée et le son se creuse, liquide.");
    describe ({ &cross }, "Cross : la part du feedback qui change de côté, de la gauche vers la droite et inversement.");
    describe ({ &mix }, "Mix : de l'entrée seule (0) à l'effet seul (100). À 50, les creux du flanger sont les plus profonds.");
    describe ({ &output }, "Out : le niveau de tout ce qui sort.");
    hoverTip.setInterceptsMouseClicks (false, false);
    hoverTip.setAlwaysOnTop (true);
    addChildComponent (hoverTip);

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("MODBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);

    setResizable (true, true);
    setResizeLimits (1120, 620, 2200, 1250);
    setSize (1240, 690);

    timerCallback();
    startTimerHz (30);
}

ModBoxEditor::~ModBoxEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void ModBoxEditor::refreshPresets()
{
    // What the two arrows step through: the presets the browser is narrowed to.
    presetNames = processor.getFilteredPresets();
}

void ModBoxEditor::stepPreset (int direction)
{
    if (presetNames.isEmpty())
        return;

    const auto count = presetNames.size();
    const auto current = presetNames.indexOf (processor.getPresetName());
    const auto next = current < 0 ? 0 : (current + direction + count) % count;

    processor.loadPreset (presetNames[next]);
}

void ModBoxEditor::beginSaving()
{
    const auto current = processor.getPresetName();

    presetNameEditor.setText ({}, dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.showEditor();

    // The name on show is there, selected: Enter keeps it, typing replaces
    // it, Escape gives up. A stock preset's name comes back with "(user)".
    if (auto* editor = presetNameEditor.getCurrentTextEditor())
    {
        // Where the menu wrote it: the same inset, on the middle line.
        editor->setBorder ({ 0, 13, 0, 30 });
        editor->setIndents (0, 0);
        editor->setJustification (Justification::centredLeft);
        editor->setText (current, false);
        editor->selectAll();
    }
}

void ModBoxEditor::deleteClicked()
{
    // Deleting is for good, so it takes two clicks: the first asks.
    if (deleteArmed == 0)
    {
        deleteArmed = 90;
        return;
    }

    deleteArmed = 0;
    processor.deleteUserPreset (processor.getPresetName());
    refreshPresets();
}

void ModBoxEditor::timerCallback()
{
    // What a preset, the other slot or the host may have changed under us.
    const auto name = processor.getPresetName();

    if (presetBox.getText() != name)
        presetBox.setText (name, dontSendNotification);

    if (browser.isVisible())
        browser.repaint();

    if (deleteArmed > 0)
        --deleteArmed;

    deleteButton.setEnabled (processor.isUserPreset (name));
    deleteButton.setButtonText (deleteArmed > 0 ? "SURE ?" : "DELETE");
    deleteButton.setToggleState (deleteArmed > 0, dontSendNotification);

    const auto slot = processor.getActiveSlot();

    slotAButton.setToggleState (slot == 0, dontSendNotification);
    slotBButton.setToggleState (slot == 1, dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "A > B" : "B > A");

    // What counts for nothing as things stand is drawn fainter: the details
    // of a circuit there is none of, the rate of an LFO that is not used,
    // the crossing of a feedback that is at zero.
    const auto analog = circuit.slider.getValue() > 0.0;

    for (auto* knob : { &drive, &drift, &noise, &stages })
        knob->setAlpha (analog ? 1.0f : 0.4f);

    rate2.setAlpha (lfo2.slider.getValue() > 0.0 ? 1.0f : 0.4f);
    cross.setAlpha (feedback.slider.getValue() != 0.0 ? 1.0f : 0.4f);
    spread.setAlpha (voices.slider.getValue() > 1.0 ? 1.0f : 0.4f);

    sweep.repaint();
    updateHoverTip();

    // The header is the only part of the frame that moves by itself.
    repaint (0, 0, getWidth(), kHeaderHeight);
}

void ModBoxEditor::describe (std::initializer_list<Component*> components, const char* text)
{
    // The descriptions are written in French, accents and all.
    for (auto* c : components)
        described.push_back ({ c, String::fromUTF8 (text) });
}

void ModBoxEditor::updateHoverTip()
{
    const auto now = Time::getMillisecondCounter();
    const auto at = getMouseXYRelative();

    // Only while the pointer is over this window with no button held.
    auto* under = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    const auto here = under != nullptr && (under == this || isParentOf (under))
                      && ! ModifierKeys::currentModifiers.isAnyMouseButtonDown();

    int over = -1;

    // Nothing under the browser is being pointed at.
    if (here && ! browser.isVisible())
        for (int i = 0; i < (int) described.size(); ++i)
            if (auto* c = described[(size_t) i].component; c->isShowing() && getLocalArea (c, c->getLocalBounds()).contains (at))
                over = i;

    // Leaving the control takes the description away, and starts the wait
    // for the next one.
    if (over != resting)
    {
        resting = over;
        restingAt = at;
        restingSince = now;
        hoverTip.setVisible (false);
        return;
    }

    // Once it is there it stays for as long as the pointer is on the
    // control, moving or not.
    if (resting < 0 || hoverTip.isVisible())
        return;

    // Until then, any movement starts the two seconds again.
    if (at.getDistanceFrom (restingAt) > 3)
    {
        restingAt = at;
        restingSince = now;
        return;
    }

    if (now - restingSince < 2000)
        return;

    const auto& text = described[(size_t) resting].text;

    hoverTip.text = text;
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (text, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (true);
    hoverTip.toFront (false);
    hoverTip.repaint();
}

int ModBoxEditor::titleRight() const
{
    return kTitleLeft
           + roundToInt (GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE")
                         + GlyphArrangement::getStringWidth (titleFont (false), "MODBOX"))
           + 12;
}

void ModBoxEditor::paintHeader (Graphics& g, Rectangle<int> header)
{
    const auto area = header.toFloat();
    const auto seconds = (Time::getMillisecondCounterHiRes() - openedAt) * 0.001;

    // The crimson of the range: darkest at the sigil, brightest behind the
    // name, and falling away again to the right and towards the foot.
    ColourGradient ground (Colour (0xff2c060b), 0.0f, 0.0f, Colour (0xff3b080e), area.getRight(), 0.0f, false);
    ground.addColour (0.32, Colour (0xff7a0e1c));
    ground.addColour (0.7, Colour (0xff5a0a14));
    g.setGradientFill (ground);
    g.fillRect (area);

    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, area.getY(),
                                       Colour (0x73000000), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (logo.isValid())
    {
        const auto logoWidth = (float) logo.getWidth() * 0.5f;
        const auto logoHeight = (float) logo.getHeight() * 0.5f;

        g.drawImage (logo, { 16.0f - (float) kLogoMargin, area.getCentreY() - logoHeight * 0.5f, logoWidth, logoHeight });
    }

    // The name: the house in white, the product in a paler weight beside it.
    const auto nameWidth = GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE");

    g.setColour (Colours::white);
    g.setFont (titleFont (true));
    g.drawText ("ONIRIQUE", area.withTrimmedLeft ((float) kTitleLeft).withWidth (nameWidth + 4.0f),
                Justification::centredLeft, false);

    g.setColour (Colour (0xffe3b5ba));
    g.setFont (titleFont (false));
    g.drawText ("MODBOX", area.withTrimmedLeft ((float) kTitleLeft + nameWidth + 10.0f).withWidth (280.0f),
                Justification::centredLeft, false);

    // Stars beside it, each breathing at its own pace.
    struct Spark { float x, y, radius, rate, offset; };
    static const Spark sparks[] = { { 10.0f, 17.0f, 5.5f, 0.9f, 0.0f },  { 27.0f, 11.0f, 3.0f, 1.3f, 1.7f },
                                    { 38.0f, 24.0f, 4.0f, 0.7f, 3.1f },  { 18.0f, 42.0f, 4.5f, 1.1f, 4.4f },
                                    { 34.0f, 48.0f, 2.5f, 1.6f, 2.3f },  { 46.0f, 38.0f, 2.0f, 1.0f, 5.2f } };

    const auto sparkLeft = (float) titleRight();

    for (const auto& s : sparks)
    {
        const auto glow = 0.5f + 0.5f * (float) std::sin (seconds * s.rate * 2.0 + s.offset);

        g.setColour (Colours::white.withAlpha (0.25f + 0.65f * glow));
        g.fillPath (star ({ sparkLeft + s.x, area.getY() + s.y }, s.radius * (0.8f + 0.2f * glow)));
    }

    // The foot of the header: a faint rule, and a streak of light that runs
    // along it from left to right and comes round again.
    const auto footY = area.getBottom() - 2.0f;

    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (0.0f, footY, area.getWidth(), 1.0f);

    const auto length = jmax (160.0f, area.getWidth() * 0.22f);
    const auto cycle = 3.6;
    const auto travelled = (float) (std::fmod (seconds, cycle) / cycle);
    const auto head = -40.0f + travelled * (area.getWidth() + length + 80.0f);
    const auto tail = head - length;

    // Bright at the head, trailing off behind it, with a halo around the lot.
    ColourGradient halo (Colours::transparentBlack, tail, 0.0f, Colours::transparentBlack, head + 14.0f, 0.0f, false);
    halo.addColour (0.82, ember.withAlpha (0.55f));
    g.setGradientFill (halo);
    g.fillRect (tail, footY - 3.0f, length + 14.0f, 7.0f);

    ColourGradient streak (Colours::transparentWhite, tail, 0.0f, Colours::transparentWhite, head + 6.0f, 0.0f, false);
    streak.addColour (0.55, emberLit.withAlpha (0.9f));
    streak.addColour (0.93, Colours::white);
    g.setGradientFill (streak);
    g.fillRoundedRectangle (tail, footY - 0.5f, length + 6.0f, 2.0f, 1.0f);
}

void ModBoxEditor::paint (Graphics& g)
{
    g.fillAll (night);

    paintHeader (g, getLocalBounds().removeFromTop (kHeaderHeight));

    // Each family on its own ground, a shade redder than the rest, under
    // its name.
    for (int i = 0; i < kNumPanels; ++i)
    {
        const auto panel = panels[i].toFloat().reduced (0.5f);

        g.setColour (deep);
        g.fillRoundedRectangle (panel, 10.0f);
        g.setColour (line);
        g.drawRoundedRectangle (panel, 10.0f, 1.0f);

        g.setColour (haze);
        g.setFont (capsFont (9.5f).withExtraKerningFactor (0.3f));
        g.drawText (kPanelNames[i], panels[i].withHeight (kPanelHeading).translated (0, 2), Justification::centred, false);
    }

    g.setColour (mist);
    g.setFont (capsFont (10.0f));
    g.drawText ("SHAPE", shapeHeading, Justification::centred, false);
}

Rectangle<int> ModBoxEditor::cell (int panel, int row, int column) const
{
    auto area = panels[panel].reduced (8, 0).withTrimmedTop (kPanelHeading).withTrimmedBottom (10);
    const auto rowHeight = (area.getHeight() - 8) / 2;
    const auto width = area.getWidth() / kPanelColumns[panel];

    return { area.getX() + column * width, area.getY() + row * (rowHeight + 8), width, rowHeight };
}

void ModBoxEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderHeight);

    // Controls sit in the upper part of the header, clear of the light that
    // runs along its foot.
    auto row = header.withTrimmedBottom (6).withSizeKeepingCentre (header.getWidth(), 30).withTrimmedRight (14);

    bypassButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (10);

    // The licence badge is one of the header's buttons, next to Bypass: the same height, the same capsule.
    // Its room is that of its longest wording; it sits against Bypass whatever it says.
    const auto badge = row.removeFromRight (150);
    row.removeFromRight (18);

    copyButton.setBounds (row.removeFromRight (64));
    row.removeFromRight (4);
    slotBButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (4);
    slotAButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (18);

    deleteButton.setBounds (row.removeFromRight (74));
    row.removeFromRight (4);
    saveButton.setBounds (row.removeFromRight (60));
    row.removeFromRight (8);

    // The name takes what is left between the title and the buttons.
    row.removeFromLeft (titleRight() + 62);
    previousButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (6);
    presetBox.setBounds (row);
    presetNameEditor.setBounds (row);

    auto foot = bounds.removeFromBottom (kFootHeight).reduced (14, 0).withTrimmedBottom (12).withTrimmedTop (4);
    output.setBounds (foot.removeFromRight (100));
    foot.removeFromRight (10);
    autoGainButton.setBounds (foot.removeFromRight (104).withSizeKeepingCentre (104, 26));

    // The window grows by giving its contents more room, not by zooming them: the scale is 1.
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), 1.0f, badge, Justification::centredRight);

    // The families take the lower half, the sweep what is left above them.
    auto lower = bounds.removeFromBottom (jlimit (270, 360, bounds.getHeight() * 52 / 100)).reduced (14, 0);
    const auto unit = (lower.getWidth() - (kNumPanels - 1) * kPanelGap) / kColumns;

    for (int i = 0; i < kNumPanels; ++i)
    {
        panels[i] = i == kNumPanels - 1 ? lower : lower.removeFromLeft (unit * kPanelColumns[i]);
        lower.removeFromLeft (kPanelGap);
    }

    sweep.setBounds (bounds.reduced (14, 12));

    // The browser lies over everything under the header.
    browser.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (14, 12));

    circuit.setBounds (cell (0, 0, 0));
    drive.setBounds (cell (0, 0, 1));
    drift.setBounds (cell (0, 1, 0));
    noise.setBounds (cell (0, 1, 1));

    voices.setBounds (cell (1, 0, 0));
    spread.setBounds (cell (1, 0, 1));
    stages.setBounds (cell (1, 1, 0));
    keep.setBounds (cell (1, 1, 1));

    rate.setBounds (cell (2, 0, 0));
    depth.setBounds (cell (2, 0, 1));
    stereo.setBounds (cell (2, 0, 2));
    rate2.setBounds (cell (2, 1, 0));
    lfo2.setBounds (cell (2, 1, 1));

    // The shape has no dial: two buttons, one over the other, in its place.
    auto shape = cell (2, 1, 2);
    shapeHeading = shape.removeFromTop (15);

    auto buttons = shape.withSizeKeepingCentre (jmin (84, shape.getWidth() - 12), 26 + 6 + 26);
    sineButton.setBounds (buttons.removeFromTop (26));
    buttons.removeFromTop (6);
    triangleButton.setBounds (buttons.removeFromTop (26));

    delay.setBounds (cell (3, 0, 0));
    feedback.setBounds (cell (3, 0, 1));
    cross.setBounds (cell (3, 1, 0));
    mix.setBounds (cell (3, 1, 1));

    crush.setBounds (cell (4, 0, 0));
    downsample.setBounds (cell (4, 0, 1));
    distortion.setBounds (cell (4, 1, 0).getUnion (cell (4, 1, 1)).withSizeKeepingCentre (cell (4, 1, 0).getWidth(), cell (4, 1, 0).getHeight()));
}

} // namespace onirique

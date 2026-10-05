#include "PluginEditor.h"

using LF = AssblasterLookAndFeel;

void AssblasterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                              float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight(), 54.0f) * 0.5f; // capped so all knobs match
    const auto c = b.getCentre();

    g.setColour (black.withAlpha (0.35f));
    g.fillEllipse (c.x - r - 2.0f, c.y - r - 1.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f); // soft shadow
    g.setColour (black);
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c.x - r + 1.5f, c.y - r + 1.5f, 2.0f * r - 3.0f, 2.0f * r - 3.0f, 1.2f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path p;
    p.addRoundedRectangle (-2.0f, -r + 3.0f, 4.0f, r * 0.5f, 1.5f);
    g.setColour (yellow);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

void AssblasterLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& t, bool, bool)
{
    auto b = t.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = t.getToggleState();

    g.setColour (black);
    g.fillRoundedRectangle (b, 4.0f);

    const float led = 10.0f;
    juce::Rectangle<float> ledR (b.getX() + 8.0f, b.getCentreY() - led * 0.5f, led, led);
    g.setColour (on ? juce::Colour (0xffff3b1f) : juce::Colour (0xff4a1a12));
    g.fillEllipse (ledR);
    if (on)
    {
        g.setColour (juce::Colour (0xffff3b1f).withAlpha (0.35f));
        g.fillEllipse (ledR.expanded (3.0f));
    }

    g.setColour (yellow);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (t.getButtonText(), b.withTrimmedLeft (led + 16.0f), juce::Justification::centredLeft, false);
}

AssblasterEditor::Knob& AssblasterEditor::addKnob (const juce::String& id, const juce::String& text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    k->slider.setColour (juce::Slider::textBoxTextColourId, LF::black);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k->slider.setDoubleClickReturnValue (true, 0.0, juce::ModifierKeys::noModifiers); // overridden by attachment default
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, LF::black);
    k->label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

AssblasterEditor::Switch& AssblasterEditor::addSwitch (const juce::String& id, const juce::String& text)
{
    auto s = std::make_unique<Switch>();
    s->button.setButtonText (text);
    s->attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, s->button);
    addAndMakeVisible (s->button);
    switches.push_back (std::move (s));
    return *switches.back();
}

AssblasterEditor::AssblasterEditor (AssblasterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    sections.push_back ({ "PREAMP",  { &addKnob ("in_db", "INPUT dB"), &addKnob ("screen", "V2 SCREEN"), &addKnob ("level_db", "LEVEL dB") }, {}, {} });
    sections.push_back ({ "PULSER / RING", { &addKnob ("pulser_amt", "AMOUNT"), &addKnob ("pulser_thr", "THRESH"),
                                             &addKnob ("pulser_rat", "RATIO x"), &addKnob ("ring", "RING MOD") },
                          { &addSwitch ("pulser_on", "PULSER") }, {} });
    sections.push_back ({ "THYRATRON VCO", { &addKnob ("vco_pitch", "PITCH Hz"), &addKnob ("vco_level", "LEVEL"), &addKnob ("vco_grid", "GRID") },
                          { &addSwitch ("vco_on", "VCO") }, {} });
    sections.push_back ({ "FILTER", { &addKnob ("filt_hz", "FREQ Hz"), &addKnob ("filt_q", "Q"),
                                      &addKnob ("filt_env", "ENV"), &addKnob ("env_decay", "DECAY ms") },
                          { &addSwitch ("filt_on", "FILTER") }, {} });
    sections.push_back ({ "GATE / OUT", { &addKnob ("gate", "GATE"), &addKnob ("master_db", "MASTER dB"), &addKnob ("mix", "MIX") },
                          { &addSwitch ("chaos", "CHAOS"), &addSwitch ("bypass", "BYPASS") }, {} });

    setSize (1180, 250);
}

AssblasterEditor::~AssblasterEditor() { setLookAndFeel (nullptr); }

void AssblasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (LF::yellow);
    g.setColour (LF::black);
    g.drawRect (getLocalBounds(), 4);

    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("THE ASSBLASTER", 20, 8, 400, 34, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("unofficial software model inspired by the Metasonix KV-100  |  not affiliated with Metasonix",
                getWidth() - 560, 12, 540, 20, juce::Justification::centredRight);

    for (auto& s : sections)
    {
        g.setColour (LF::black.withAlpha (0.6f));
        g.drawRoundedRectangle (s.bounds.toFloat(), 6.0f, 1.5f);
        g.setColour (LF::black);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.getX() + 8, s.bounds.getY() + 4, s.bounds.getWidth() - 16, 16,
                    juce::Justification::centredLeft);
    }
}

void AssblasterEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (38);

    // width proportional to number of knobs (+ a little for switches)
    auto weight = [] (const Section& s) { return (int) s.knobs.size() + (s.switches.size() > 1 ? 1 : 0); };
    int totalUnits = 0;
    for (auto& s : sections)
        totalUnits += weight (s);

    const int gap = 8;
    const int usable = area.getWidth() - gap * ((int) sections.size() - 1);
    const float unit = (float) usable / (float) totalUnits;

    int x = area.getX();
    for (auto& s : sections)
    {
        const int w = juce::roundToInt (unit * (float) weight (s));
        s.bounds = { x, area.getY(), w, area.getHeight() };
        x += w + gap;

        auto inner = s.bounds.reduced (6);
        inner.removeFromTop (18);

        // switches along the bottom
        if (! s.switches.empty())
        {
            auto sw = inner.removeFromBottom (30);
            const int each = sw.getWidth() / (int) s.switches.size();
            for (auto* b : s.switches)
                b->button.setBounds (sw.removeFromLeft (each).reduced (2, 0));
        }

        const int kw = inner.getWidth() / (int) s.knobs.size();
        for (auto* k : s.knobs)
        {
            auto col = inner.removeFromLeft (kw);
            k->label.setBounds (col.removeFromTop (18));
            k->slider.setBounds (col);
        }
    }
}

#pragma once

#include "PluginProcessor.h"
#include <memory>
#include <vector>

// Yellow-and-black "boutique tube box" look. Purely cosmetic.
class AssblasterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour yellow { 0xfff2c200 };
    static inline const juce::Colour black  { 0xff141414 };

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class AssblasterEditor : public juce::AudioProcessorEditor
{
public:
    explicit AssblasterEditor (AssblasterProcessor&);
    ~AssblasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    struct Switch
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
    };
    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        std::vector<Switch*> switches;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& text);
    Switch& addSwitch (const juce::String& paramId, const juce::String& text);

    AssblasterProcessor& processor;
    AssblasterLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssblasterEditor)
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cigol
{
class AppLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour::fromRGB(24, 27, 34));
        setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
        setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(240, 240, 240));
        setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(255, 162, 72));
        setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 61, 74));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(255, 112, 74));
    }
};
} // namespace cigol

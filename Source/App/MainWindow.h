#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cigol
{
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(juce::String name);

    void closeButtonPressed() override;
};
} // namespace cigol

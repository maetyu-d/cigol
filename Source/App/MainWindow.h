#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace cigol
{
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(juce::String name);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    std::unique_ptr<juce::MenuBarModel> menuModel;
};
} // namespace cigol

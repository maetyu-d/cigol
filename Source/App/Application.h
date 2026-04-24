#pragma once

#include "MainWindow.h"
#include "../UI/AppLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace logiclikedaw
{
class Application final : public juce::JUCEApplication
{
public:
    ~Application() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;

    void initialise(const juce::String&) override;
    void shutdown() override;

private:
    std::unique_ptr<AppLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace logiclikedaw

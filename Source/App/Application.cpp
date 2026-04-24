#include "Application.h"

namespace logiclikedaw
{
Application::~Application() = default;

const juce::String Application::getApplicationName()
{
    return "LogicLikeDAW";
}

const juce::String Application::getApplicationVersion()
{
    return "0.2.0";
}

void Application::initialise(const juce::String&)
{
    lookAndFeel = std::make_unique<AppLookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());
    mainWindow = std::make_unique<MainWindow>(getApplicationName());
}

void Application::shutdown()
{
    mainWindow.reset();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    lookAndFeel.reset();
}
} // namespace logiclikedaw

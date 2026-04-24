#include "MainWindow.h"

#include "../UI/MainComponent.h"

namespace logiclikedaw
{
MainWindow::MainWindow(juce::String name)
    : juce::DocumentWindow(std::move(name),
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(1180, 760, 2600, 1800);
    setContentOwned(new MainComponent(), true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace logiclikedaw

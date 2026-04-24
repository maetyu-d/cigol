#pragma once

#include "../Core/SessionModel.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/SuperColliderBridge.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace logiclikedaw
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void selectTrack(int trackId);
    void selectRegion(int trackId, int regionIndex);
    void regionEdited();
    void refreshAllViews(bool refreshLayout = false);
    void markSessionChanged(bool needsLayoutRefresh = false, bool commitImmediately = false);
    void commitUndoSnapshotNow();
    void applySessionSnapshot(const juce::String& snapshotJson);
    void performUndo();
    void performRedo();
    void saveProject(bool saveAs);
    void loadProject();
    void updateWindowState();
    void rebuildSynthDefs();
    void assignAudioFileToSelectedRegion();
    void clearAudioFileFromSelectedRegion();
    void loadAudioUnitIntoSelectedTrack(int slotIndex);
    void clearAudioUnitFromSelectedTrack(int slotIndex);
    void openAudioUnitEditorForSelectedTrack(int slotIndex);
    void closeAudioUnitEditorWindow(int trackId, int slotIndex);
    void closeAllAudioUnitEditorWindows();

    SessionState session { createDemoSession() };
    SuperColliderProcessBridge superColliderBridge;
    juce::AudioDeviceManager deviceManager;
    AudioEngine audioEngine;

    class TransportComponent;
    class ArrangeViewComponent;
    class InspectorComponent;
    class MixerComponent;
    class PianoRollComponent;
    class SuperColliderOverviewComponent;
    class PluginEditorWindow;

    std::unique_ptr<TransportComponent> transport;
    std::unique_ptr<ArrangeViewComponent> arrangeView;
    std::unique_ptr<InspectorComponent> inspector;
    std::unique_ptr<MixerComponent> mixer;
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<SuperColliderOverviewComponent> superColliderOverview;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    std::unique_ptr<juce::FileChooser> activeProjectChooser;
    std::vector<std::unique_ptr<PluginEditorWindow>> pluginEditorWindows;
    std::vector<juce::String> undoSnapshots;
    std::vector<juce::String> redoSnapshots;
    juce::String currentProjectPath;
    bool sessionDirty { false };
    bool undoSnapshotPending { false };
    bool suppressUndoCapture { false };
    uint32_t lastMutationTimeMs { 0 };
    bool synthDefRebuildInProgress { false };
};
} // namespace logiclikedaw

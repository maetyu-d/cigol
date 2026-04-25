#pragma once

#include "../Core/SessionModel.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/SuperColliderBridge.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace cigol
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    enum class SuperColliderRenderMode
    {
        newTrack,
        replacePrintTrack,
        cycleToNewTrack
    };

    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void menuNewProject();
    void menuOpenProject();
    void menuSaveProject();
    void menuSaveProjectAs();
    void menuImportAudioToSelectedClip();
    void menuUndo();
    void menuRedo();
    void menuCut();
    void menuCopy();
    void menuPaste();
    void menuDelete();
    void menuSplitAtPlayhead();
    void menuSplitAtLocators();
    void menuGlueRegions();
    void menuSelectAllInCycle();
    void menuSelectOverlapping();
    void menuRepeatRegionByCycle();
    void menuRepeatRegionByCount();
    void menuAddAudioTrack(bool mono, int count = 1);
    void menuAddMidiTrack(int count = 1);
    void menuAddInstrumentTrack(int count = 1);
    void menuAddSuperColliderTrack(int count = 1);
    void menuAddFolderTrack();
    void menuDuplicateTrack();
    void menuDuplicateTrackWithContent();
    void menuRemoveTrack();
    void menuRenameTrack();
    void menuShowEditors();
    void menuShowMixer();
    void menuShowSplit();
    void menuToggleLowerPane();
    void menuToggleTrackList();
    void menuToggleInspector();
    void menuResetLayout();
    void menuReturnToStart();
    void menuPlayPause();
    void menuStop();
    void menuToggleRecord();
    void menuToggleCycle();
    void menuSetCycleLeftToPlayhead();
    void menuSetCycleRightToPlayhead();
    void menuAbout();
    void menuShowShortcuts();
    bool canUndoAction() const;
    bool canRedoAction() const;
    bool canCutAction() const;
    bool canCopyAction() const;
    bool canPasteAction() const;
    bool canDeleteAction() const;
    bool canRemoveTrackAction() const;
    bool canDuplicateTrackAction() const;
    bool isLowerPaneExpandedState() const;
    bool isPlayingState() const;
    bool isRecordingState() const;

    enum class LowerPaneMode
    {
        editor,
        mixer,
        split
    };

private:
    void restoreLayoutStateFromSession();
    void syncLayoutStateToSession(bool markDirty);
    void syncSelectionSpecificLayoutState(bool markDirty);
    void restoreSelectionSpecificLayoutState();
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
    void openAddTrackDialog();
    void addTracks(TrackKind kind, TrackChannelMode channelMode, int count);
    void removeSelectedTrack();
    void duplicateSelectedTrack(bool includeContent);
    void deleteSelectedRegionOrTrack();
    void copySelectedRegion();
    void cutSelectedRegion();
    void pasteCopiedRegion();
    void duplicateSelectedRegion();
    void createNewSuperColliderClip();
    void renderSelectedSuperColliderClipToAudio(SuperColliderRenderMode mode);
    void toggleSelectedRegionLooping();
    bool splitTargetRegionsAtBeat(double beat);
    bool splitTargetRegionsAtLocators();
    bool glueTargetRegions();
    bool repeatSelectedRegionByCycle();
    bool repeatSelectedRegionByCount(int repeatCount);
    double currentNudgeAmountInBeats() const;
    bool nudgeSelectedRegion(double beatDelta);
    bool nudgeSelectedMidiNotes(double beatDelta);
    void selectRegionsInBeatRange(double startBeat, double endBeat, bool overlappingOnly);
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
    class AudioClipEditorComponent;
    class SuperColliderCodeEditorComponent;
    class LowerPaneSplitterComponent;
    class RightSidebarSplitterComponent;
    class SuperColliderOverviewComponent;
    class PluginEditorWindow;

    std::unique_ptr<TransportComponent> transport;
    std::unique_ptr<ArrangeViewComponent> arrangeView;
    std::unique_ptr<InspectorComponent> inspector;
    std::unique_ptr<MixerComponent> mixer;
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<AudioClipEditorComponent> audioClipEditor;
    std::unique_ptr<SuperColliderCodeEditorComponent> superColliderCodeEditor;
    std::unique_ptr<LowerPaneSplitterComponent> lowerPaneSplitter;
    std::unique_ptr<RightSidebarSplitterComponent> rightSidebarSplitter;
    std::unique_ptr<SuperColliderOverviewComponent> superColliderOverview;
    juce::Viewport rightSidebarViewport;
    juce::Component rightSidebarContent;
    juce::TextButton rightSidebarUtilityToggleButton;
    juce::TextButton editorPaneButton;
    juce::TextButton mixerPaneButton;
    juce::TextButton splitPaneButton;
    juce::TextButton lowerPaneToggleButton;
    juce::Label lowerPaneTitleLabel;
    juce::TextButton editorZoomOutButton;
    juce::TextButton editorZoomInButton;
    juce::TextButton editorPrimaryToolButton;
    juce::TextButton editorSecondaryToolButton;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    std::unique_ptr<juce::FileChooser> activeProjectChooser;
    std::vector<std::unique_ptr<PluginEditorWindow>> pluginEditorWindows;
    std::vector<juce::String> undoSnapshots;
    std::vector<juce::String> redoSnapshots;
    juce::String currentProjectPath;
    std::optional<Region> copiedRegion;
    bool sessionDirty { false };
    bool undoSnapshotPending { false };
    bool suppressUndoCapture { false };
    uint32_t lastMutationTimeMs { 0 };
    bool synthDefRebuildInProgress { false };
    bool lowerPaneExpanded { true };
    LowerPaneMode lowerPaneMode { LowerPaneMode::editor };
    int lowerPaneHeight { 318 };
    int rightSidebarWidth { 392 };
    bool superColliderOverviewVisible { false };
};
} // namespace cigol

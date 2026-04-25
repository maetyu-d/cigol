#include "MainWindow.h"

#include "../UI/MainComponent.h"

namespace cigol
{
namespace
{
enum MenuItemIds
{
    menuNewProject = 1001,
    menuOpenProject,
    menuSaveProject,
    menuSaveProjectAs,
    menuImportAudioToSelectedClip,
    menuUndo,
    menuRedo,
    menuCut,
    menuCopy,
    menuPaste,
    menuDelete,
    menuSplitAtPlayhead,
    menuSplitAtLocators,
    menuGlueRegions,
    menuSelectAllInCycle,
    menuSelectOverlapping,
    menuRepeatRegionByCycle,
    menuRepeatRegionByCount,
    menuAddAudioTrackStereo,
    menuAddAudioTrackMono,
    menuAddMidiTrack,
    menuAddInstrumentTrack,
    menuAddSuperColliderTrack,
    menuAddFolderTrack,
    menuDuplicateTrack,
    menuDuplicateTrackWithContent,
    menuRemoveTrack,
    menuRenameTrack,
    menuShowEditors,
    menuShowMixer,
    menuShowSplit,
    menuToggleLowerPane,
    menuToggleTrackList,
    menuToggleInspector,
    menuResetLayout,
    menuReturnToStart,
    menuPlayPause,
    menuStop,
    menuToggleRecord,
    menuToggleCycle,
    menuSetCycleLeftToPlayhead,
    menuSetCycleRightToPlayhead,
    menuAbout,
    menuKeyboardShortcuts
};

class MainMenuModel final : public juce::MenuBarModel
{
public:
    explicit MainMenuModel(MainWindow& ownerToUse) : owner(ownerToUse) {}

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Track", "View", "Window", "Transport", "Help" };
    }

    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String&) override
    {
        juce::PopupMenu menu;
        auto* main = getMainComponent();

        switch (menuIndex)
        {
            case 0:
                menu.addItem(menuNewProject, "New Project\tCmd+N");
                menu.addItem(menuOpenProject, "Open...\tCmd+O");
                menu.addSeparator();
                menu.addItem(menuSaveProject, "Save\tCmd+S", main != nullptr);
                menu.addItem(menuSaveProjectAs, "Save As...\tCmd+Shift+S", main != nullptr);
                menu.addSeparator();
                menu.addItem(menuImportAudioToSelectedClip, "Import Audio to Selected Clip...", main != nullptr);
                break;

            case 1:
                menu.addItem(menuUndo, "Undo\tCmd+Z", main != nullptr && main->canUndoAction());
                menu.addItem(menuRedo, "Redo\tCmd+Shift+Z", main != nullptr && main->canRedoAction());
                menu.addSeparator();
                menu.addItem(menuCut, "Cut\tCmd+X", main != nullptr && main->canCutAction());
                menu.addItem(menuCopy, "Copy\tCmd+C", main != nullptr && main->canCopyAction());
                menu.addItem(menuPaste, "Paste\tCmd+V", main != nullptr && main->canPasteAction());
                menu.addItem(menuDelete, "Delete\tDelete", main != nullptr && main->canDeleteAction());
                menu.addSeparator();
                menu.addItem(menuSplitAtPlayhead, "Split at Playhead\tCmd+\\", main != nullptr);
                menu.addItem(menuSplitAtLocators, "Split at Locators\tCmd+Shift+\\", main != nullptr);
                menu.addItem(menuGlueRegions, "Glue Regions\tCmd+J", main != nullptr);
                menu.addSeparator();
                menu.addItem(menuSelectAllInCycle, "Select All in Cycle\tCmd+Shift+A", main != nullptr);
                menu.addItem(menuSelectOverlapping, "Select Overlapping\tCmd+Option+A", main != nullptr);
                menu.addSeparator();
                menu.addItem(menuRepeatRegionByCycle, "Repeat Region by Cycle\tCmd+Option+R", main != nullptr);
                menu.addItem(menuRepeatRegionByCount, "Repeat Region by Count...\tCmd+Shift+Option+R", main != nullptr);
                break;

            case 2:
            {
                juce::PopupMenu newTrackMenu;
                newTrackMenu.addItem(menuAddAudioTrackStereo, "New Audio Track (Stereo)");
                newTrackMenu.addItem(menuAddAudioTrackMono, "New Audio Track (Mono)");
                newTrackMenu.addItem(menuAddMidiTrack, "New MIDI Track");
                newTrackMenu.addItem(menuAddInstrumentTrack, "New Instrument Track");
                newTrackMenu.addItem(menuAddSuperColliderTrack, "New SC Scene Track");
                newTrackMenu.addItem(menuAddFolderTrack, "New Folder Stack");
                menu.addSubMenu("New Track", newTrackMenu);
                menu.addSeparator();
                menu.addItem(menuDuplicateTrack, "Duplicate Track\tCmd+D", main != nullptr && main->canDuplicateTrackAction());
                menu.addItem(menuDuplicateTrackWithContent, "Duplicate Track with Content\tCmd+Shift+D", main != nullptr && main->canDuplicateTrackAction());
                menu.addItem(menuRenameTrack, "Rename Track...\tCmd+R", main != nullptr && main->canDuplicateTrackAction());
                menu.addItem(menuRemoveTrack, "Delete Track", main != nullptr && main->canRemoveTrackAction());
                break;
            }

            case 3:
                menu.addItem(menuShowEditors, "Show Editors\tE", main != nullptr);
                menu.addItem(menuShowMixer, "Show Mixer\tX", main != nullptr);
                menu.addItem(menuShowSplit, "Show Split View", main != nullptr);
                menu.addSeparator();
                menu.addItem(menuToggleLowerPane,
                             main != nullptr && main->isLowerPaneExpandedState() ? "Hide Lower Pane" : "Show Lower Pane",
                             main != nullptr);
                break;

            case 4:
                menu.addItem(menuToggleTrackList, "Show / Hide Track List\tCmd+T", main != nullptr);
                menu.addItem(menuToggleInspector, "Show / Hide Inspector\tCmd+I", main != nullptr);
                menu.addItem(menuResetLayout, "Reset Window Layout", main != nullptr);
                break;

            case 5:
                menu.addItem(menuReturnToStart, "Return to Start", main != nullptr);
                menu.addItem(menuPlayPause,
                             main != nullptr && main->isPlayingState() ? "Pause\tSpace" : "Play\tSpace",
                             main != nullptr);
                menu.addItem(menuStop, "Stop", main != nullptr);
                menu.addItem(menuToggleRecord,
                             main != nullptr && main->isRecordingState() ? "Stop Recording\tR" : "Record\tR",
                             main != nullptr);
                menu.addSeparator();
                menu.addItem(menuToggleCycle, "Toggle Cycle\tC", main != nullptr);
                menu.addItem(menuSetCycleLeftToPlayhead, "Set Left Locator to Playhead\t[", main != nullptr);
                menu.addItem(menuSetCycleRightToPlayhead, "Set Right Locator to Playhead\t]", main != nullptr);
                break;

            case 6:
                menu.addItem(menuAbout, "About cigoL");
                menu.addSeparator();
                menu.addItem(menuKeyboardShortcuts, "Keyboard Shortcuts");
                break;
        }

        return menu;
    }

    void menuItemSelected(int menuItemID, int) override
    {
        auto* main = getMainComponent();
        if (main == nullptr)
            return;

        switch (menuItemID)
        {
            case menuNewProject: main->menuNewProject(); break;
            case menuOpenProject: main->menuOpenProject(); break;
            case menuSaveProject: main->menuSaveProject(); break;
            case menuSaveProjectAs: main->menuSaveProjectAs(); break;
            case menuImportAudioToSelectedClip: main->menuImportAudioToSelectedClip(); break;
            case menuUndo: main->menuUndo(); break;
            case menuRedo: main->menuRedo(); break;
            case menuCut: main->menuCut(); break;
            case menuCopy: main->menuCopy(); break;
            case menuPaste: main->menuPaste(); break;
            case menuDelete: main->menuDelete(); break;
            case menuSplitAtPlayhead: main->menuSplitAtPlayhead(); break;
            case menuSplitAtLocators: main->menuSplitAtLocators(); break;
            case menuGlueRegions: main->menuGlueRegions(); break;
            case menuSelectAllInCycle: main->menuSelectAllInCycle(); break;
            case menuSelectOverlapping: main->menuSelectOverlapping(); break;
            case menuRepeatRegionByCycle: main->menuRepeatRegionByCycle(); break;
            case menuRepeatRegionByCount: main->menuRepeatRegionByCount(); break;
            case menuAddAudioTrackStereo: main->menuAddAudioTrack(false); break;
            case menuAddAudioTrackMono: main->menuAddAudioTrack(true); break;
            case menuAddMidiTrack: main->menuAddMidiTrack(); break;
            case menuAddInstrumentTrack: main->menuAddInstrumentTrack(); break;
            case menuAddSuperColliderTrack: main->menuAddSuperColliderTrack(); break;
            case menuAddFolderTrack: main->menuAddFolderTrack(); break;
            case menuDuplicateTrack: main->menuDuplicateTrack(); break;
            case menuDuplicateTrackWithContent: main->menuDuplicateTrackWithContent(); break;
            case menuRenameTrack: main->menuRenameTrack(); break;
            case menuRemoveTrack: main->menuRemoveTrack(); break;
            case menuShowEditors: main->menuShowEditors(); break;
            case menuShowMixer: main->menuShowMixer(); break;
            case menuShowSplit: main->menuShowSplit(); break;
            case menuToggleLowerPane: main->menuToggleLowerPane(); break;
            case menuToggleTrackList: main->menuToggleTrackList(); break;
            case menuToggleInspector: main->menuToggleInspector(); break;
            case menuResetLayout: main->menuResetLayout(); break;
            case menuReturnToStart: main->menuReturnToStart(); break;
            case menuPlayPause: main->menuPlayPause(); break;
            case menuStop: main->menuStop(); break;
            case menuToggleRecord: main->menuToggleRecord(); break;
            case menuToggleCycle: main->menuToggleCycle(); break;
            case menuSetCycleLeftToPlayhead: main->menuSetCycleLeftToPlayhead(); break;
            case menuSetCycleRightToPlayhead: main->menuSetCycleRightToPlayhead(); break;
            case menuAbout: main->menuAbout(); break;
            case menuKeyboardShortcuts: main->menuShowShortcuts(); break;
            default: break;
        }

        menuItemsChanged();
    }

private:
    MainComponent* getMainComponent() const
    {
        return dynamic_cast<MainComponent*>(owner.getContentComponent());
    }

    MainWindow& owner;
};
} // namespace

MainWindow::MainWindow(juce::String name)
    : juce::DocumentWindow(std::move(name),
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(1180, 760, 2600, 1800);
    setContentOwned(new MainComponent(), true);
    menuModel = std::make_unique<MainMenuModel>(*this);
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(menuModel.get());
#else
    setMenuBar(menuModel.get());
#endif
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainWindow::~MainWindow()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    setMenuBar(nullptr);
#endif
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace cigol

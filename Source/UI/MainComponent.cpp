#include "MainComponent.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_utils/juce_audio_utils.h>

namespace logiclikedaw
{
namespace
{
using namespace juce;

String describeProcessorChain(const TrackState& track)
{
    if (track.inserts.empty())
        return "No inserts";

    StringArray labels;

    for (const auto& insert : track.inserts)
        labels.add(insert.name + (insert.bypassed ? " (bypassed)" : ""));

    return labels.joinIntoString(" | ");
}

String describeRegionKind(const RegionKind kind)
{
    switch (kind)
    {
        case RegionKind::audio: return "Audio";
        case RegionKind::midi: return "MIDI";
        case RegionKind::generated: return "Generated";
    }

    return "Region";
}

String shortenForSidebar(const String& text, const int maxCharacters)
{
    if (text.length() <= maxCharacters || maxCharacters < 8)
        return text;

    const auto headLength = maxCharacters / 2;
    const auto tailLength = maxCharacters - headLength - 3;
    return text.substring(0, headLength) + "..." + text.substring(text.length() - tailLength);
}

float interpolateAutomationDisplayValue(const std::vector<AutomationPoint>& points, const double beat, const float fallback)
{
    if (points.empty())
        return fallback;

    if (beat <= points.front().beat)
        return points.front().value;

    if (beat >= points.back().beat)
        return points.back().value;

    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& left = points[i - 1];
        const auto& right = points[i];

        if (beat <= right.beat)
        {
            const auto span = juce::jmax(0.0001, right.beat - left.beat);
            const auto t = static_cast<float>((beat - left.beat) / span);
            const auto shapedT = [&left, t]
            {
                switch (left.shapeToNext)
                {
                    case AutomationPoint::SegmentShape::linear: return t;
                    case AutomationPoint::SegmentShape::easeIn: return t * t;
                    case AutomationPoint::SegmentShape::easeOut: return 1.0f - ((1.0f - t) * (1.0f - t));
                    case AutomationPoint::SegmentShape::step: return 0.0f;
                }

                return t;
            }();

            return juce::jmap(shapedT, left.value, right.value);
        }
    }

    return points.back().value;
}

class MeterComponent final : public Component
{
public:
    void setLevel(float newLevel)
    {
        level = jlimit(0.0f, 1.0f, newLevel);
        repaint();
    }

    void paint(Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(Colours::black.withAlpha(0.25f));
        g.fillRoundedRectangle(area, 4.0f);

        auto fill = area.reduced(3.0f);
        auto height = fill.getHeight() * level;
        auto active = Rectangle<float>(fill.getX(), fill.getBottom() - height, fill.getWidth(), height);

        ColourGradient gradient(Colour::fromRGB(62, 214, 152), active.getBottomLeft(),
                                Colour::fromRGB(255, 119, 71), active.getTopLeft(), false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(active, 3.0f);
    }

private:
    float level { 0.0f };
};
} // namespace

class MainComponent::TransportComponent final : public Component,
                                                private Timer
{
public:
    TransportComponent(TransportState& stateToUse,
                       const AudioEngine& engineToUse,
                       std::function<void()> onUndoToUse,
                       std::function<void()> onRedoToUse,
                       std::function<void()> onSaveToUse,
                       std::function<void()> onLoadToUse)
        : state(stateToUse),
          engine(engineToUse),
          onUndo(std::move(onUndoToUse)),
          onRedo(std::move(onRedoToUse)),
          onSave(std::move(onSaveToUse)),
          onLoad(std::move(onLoadToUse))
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setText("LogicLikeDAW", dontSendNotification);
        titleLabel.setFont(FontOptions(24.0f, Font::bold));

        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(Justification::centredLeft);
        statusLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.60f));

        addAndMakeVisible(projectLabel);
        projectLabel.setJustificationType(Justification::centredLeft);
        projectLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.72f));

        configureButton(backButton, "|<", [this] { state.playheadBeat = std::max(1.0, state.playheadBeat - 4.0); });
        configureButton(playButton, "Play", [this] { state.playing = ! state.playing; refreshButtonStates(); });
        configureButton(stopButton, "Stop", [this] {
            state.playing = false;
            state.recording = false;
            state.playheadBeat = 1.0;
            refreshButtonStates();
        });
        configureButton(recordButton, "Rec", [this] {
            state.recording = ! state.recording;
            state.playing = state.playing || state.recording;
            refreshButtonStates();
        });
        configureButton(undoButton, "Undo", [this] { if (onUndo != nullptr) onUndo(); });
        configureButton(redoButton, "Redo", [this] { if (onRedo != nullptr) onRedo(); });
        configureButton(saveButton, "Save", [this] { if (onSave != nullptr) onSave(); });
        configureButton(loadButton, "Load", [this] { if (onLoad != nullptr) onLoad(); });

        addAndMakeVisible(tempoSlider);
        tempoSlider.setRange(60.0, 180.0, 1.0);
        tempoSlider.setValue(state.bpm);
        tempoSlider.setTextValueSuffix(" BPM");
        tempoSlider.setSliderStyle(Slider::IncDecButtons);
        tempoSlider.setIncDecButtonsMode(Slider::incDecButtonsDraggable_AutoDirection);
        tempoSlider.onValueChange = [this] { state.bpm = tempoSlider.getValue(); };

        addAndMakeVisible(positionLabel);
        positionLabel.setJustificationType(Justification::centredRight);
        positionLabel.setFont(FontOptions(18.0f, Font::bold));

        refreshButtonStates();
        startTimerHz(30);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16, 10);

        auto left = area.removeFromLeft(340);
        titleLabel.setBounds(left.removeFromTop(30));
        projectLabel.setBounds(left.removeFromTop(20));
        statusLabel.setBounds(left);

        auto utilityButtons = area.removeFromLeft(280);
        undoButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        redoButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        saveButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        loadButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));

        auto buttons = area.removeFromLeft(268);
        backButton.setBounds(buttons.removeFromLeft(48).reduced(4));
        playButton.setBounds(buttons.removeFromLeft(72).reduced(4));
        stopButton.setBounds(buttons.removeFromLeft(72).reduced(4));
        recordButton.setBounds(buttons.removeFromLeft(72).reduced(4));

        tempoSlider.setBounds(area.removeFromLeft(160).reduced(10, 4));
        positionLabel.setBounds(area.removeFromRight(190));
    }

    void setProjectStatus(const String& projectName, const bool dirty, const bool canUndo, const bool canRedo)
    {
        projectLabel.setText((dirty ? "* " : "") + projectName, dontSendNotification);
        undoButton.setEnabled(canUndo);
        redoButton.setEnabled(canRedo);
    }

private:
    void configureButton(TextButton& button, const String& text, std::function<void()> onClick)
    {
        addAndMakeVisible(button);
        button.setButtonText(text);
        button.onClick = std::move(onClick);
    }

    void refreshButtonStates()
    {
        playButton.setToggleState(state.playing && ! state.recording, dontSendNotification);
        recordButton.setToggleState(state.recording, dontSendNotification);
    }

    void timerCallback() override
    {
        if (state.playing)
            state.playheadBeat += (state.bpm / 60.0) / 30.0;

        auto wholeBeat = static_cast<int>(state.playheadBeat);
        auto bar = ((wholeBeat - 1) / 4) + 1;
        auto beat = ((wholeBeat - 1) % 4) + 1;
        auto tick = static_cast<int>((state.playheadBeat - std::floor(state.playheadBeat)) * 960.0);

        positionLabel.setText(String(bar) + "  " + String(beat) + "  " + String(tick), dontSendNotification);
        statusLabel.setText(engine.getEngineSummary(), dontSendNotification);
    }

    TransportState& state;
    const AudioEngine& engine;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onSave;
    std::function<void()> onLoad;
    Label titleLabel;
    Label statusLabel;
    Label projectLabel;
    Label positionLabel;
    TextButton undoButton;
    TextButton redoButton;
    TextButton saveButton;
    TextButton loadButton;
    TextButton backButton;
    TextButton playButton;
    TextButton stopButton;
    TextButton recordButton;
    Slider tempoSlider;
};

class TrackHeaderComponent final : public Component
{
public:
    TrackHeaderComponent(TrackState& trackToUse,
                         std::function<void(int)> onSelectToUse,
                         std::function<void(int)> onAutomationToggleToUse,
                         std::function<void()> onTrackEditedToUse)
        : track(trackToUse),
          onSelect(std::move(onSelectToUse)),
          onAutomationToggle(std::move(onAutomationToggleToUse)),
          onTrackEdited(std::move(onTrackEditedToUse))
    {
        nameLabel.setText(track.name, dontSendNotification);
        nameLabel.setFont(FontOptions(15.0f, Font::bold));
        roleLabel.setText(track.role + " / " + toDisplayString(track.kind), dontSendNotification);
        roleLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.60f));

        addAndMakeVisible(nameLabel);
        addAndMakeVisible(roleLabel);
        addAndMakeVisible(automationButton);

        configureToggle(armButton, "R", track.armed, [this] (bool value) { track.armed = value; });
        configureToggle(muteButton, "M", track.muted, [this] (bool value) { track.muted = value; });
        configureToggle(soloButton, "S", track.solo, [this] (bool value) { track.solo = value; });

        automationButton.onClick = [this]
        {
            if (track.visibleAutomationLane == AutomationLaneMode::none)
                track.visibleAutomationLane = AutomationLaneMode::volume;

            track.automationExpanded = ! track.automationExpanded;

            if (onAutomationToggle != nullptr)
                onAutomationToggle(track.id);

            if (onTrackEdited != nullptr)
                onTrackEdited();
        };
    }

    void paint(Graphics& g) override
    {
        auto background = track.selected ? Colour::fromRGB(46, 55, 72) : Colour::fromRGB(31, 35, 44);
        g.fillAll(background);

        g.setColour(track.colour);
        g.fillRect(0, 0, 6, getHeight());

        if (track.kind == TrackKind::superColliderRender)
        {
            g.setColour(Colours::white.withAlpha(0.08f));
            g.drawFittedText("SC", getLocalBounds().removeFromRight(28), Justification::centred, 1);
        }

        g.setColour(Colours::white.withAlpha(0.10f));
        g.drawLine(0.0f, static_cast<float>(getHeight() - 1), static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1));
    }

    void resized() override
    {
        updateAutomationButton();
        auto area = getLocalBounds().reduced(12, 8);
        auto buttonArea = area.removeFromRight(122);
        nameLabel.setBounds(area.removeFromTop(22));
        roleLabel.setBounds(area.removeFromTop(18));

        automationButton.setBounds(buttonArea.removeFromLeft(26).reduced(2));
        buttonArea.removeFromLeft(4);
        armButton.setBounds(buttonArea.removeFromLeft(30).reduced(2));
        muteButton.setBounds(buttonArea.removeFromLeft(30).reduced(2));
        soloButton.setBounds(buttonArea.removeFromLeft(30).reduced(2));
    }

    void parentHierarchyChanged() override
    {
        updateAutomationButton();
    }

    void visibilityChanged() override
    {
        updateAutomationButton();
    }

    void mouseUp(const MouseEvent&) override
    {
        onSelect(track.id);
    }

private:
    void configureToggle(ToggleButton& button, const String& text, bool initialValue, std::function<void(bool)> onChange)
    {
        addAndMakeVisible(button);
        button.setButtonText(text);
        button.setToggleState(initialValue, dontSendNotification);
        button.onClick = [this, buttonPtr = &button, callback = std::move(onChange)]
        {
            callback(buttonPtr->getToggleState());
            if (onTrackEdited != nullptr)
                onTrackEdited();
        };
    }

    TrackState& track;
    std::function<void(int)> onSelect;
    std::function<void(int)> onAutomationToggle;
    std::function<void()> onTrackEdited;
    Label nameLabel;
    Label roleLabel;
    TextButton automationButton;
    ToggleButton armButton;
    ToggleButton muteButton;
    ToggleButton soloButton;

    void updateAutomationButton()
    {
        auto icon = track.automationExpanded ? "v" : ">";
        auto lane = track.visibleAutomationLane == AutomationLaneMode::pan ? "P" : "A";
        automationButton.setButtonText(juce::String(icon) + lane);
        automationButton.setColour(TextButton::buttonColourId,
                                   track.automationExpanded ? track.colour.withAlpha(0.35f) : Colour::fromRGB(54, 60, 74));
        automationButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.90f));
    }
};

class MainComponent::ArrangeViewComponent final : public Component
{
public:
    ArrangeViewComponent(SessionState& sessionToUse,
                         std::function<void(int)> onTrackSelectToUse,
                         std::function<void(int, int)> onRegionSelectToUse,
                         std::function<void()> onRegionEditToUse,
                         std::function<void()> onAutomationLayoutChangeToUse,
                         std::function<void()> onTrackEditToUse)
        : session(sessionToUse),
          onTrackSelect(std::move(onTrackSelectToUse)),
          onRegionSelect(std::move(onRegionSelectToUse)),
          onRegionEdit(std::move(onRegionEditToUse)),
          onAutomationLayoutChange(std::move(onAutomationLayoutChangeToUse)),
          onTrackEdit(std::move(onTrackEditToUse)),
          thumbnailCache(24)
    {
        audioFormatManager.registerBasicFormats();
        setWantsKeyboardFocus(true);
        rebuildTrackHeaders();
    }

    void refreshTracks()
    {
        rebuildTrackHeaders();
        repaint();
    }

    void paint(Graphics& g) override
    {
        auto area = getLocalBounds();
        g.fillAll(Colour::fromRGB(20, 23, 30));

        auto gridArea = area.withTrimmedLeft(headerWidth).withTrimmedTop(timelineHeight);
        paintTimeline(g, area.removeFromTop(timelineHeight).withTrimmedLeft(headerWidth));

        for (int i = 0; i < static_cast<int>(session.tracks.size()); ++i)
        {
            auto& track = session.tracks[static_cast<size_t>(i)];
            auto row = trackRowBounds(i);
            auto regionRow = trackRegionBounds(i);
            auto automationRow = trackAutomationBounds(i);

            g.setColour(i % 2 == 0 ? Colour::fromRGB(27, 31, 39) : Colour::fromRGB(24, 28, 35));
            g.fillRect(row);
            g.setColour(Colours::white.withAlpha(0.05f));
            g.drawLine(static_cast<float>(row.getX()), static_cast<float>(row.getBottom()), static_cast<float>(row.getRight()), static_cast<float>(row.getBottom()));

            paintBarGrid(g, row);
            paintTrackRegions(g, regionRow, track);

            if (! automationRow.isEmpty())
            {
                g.setColour(Colours::black.withAlpha(0.08f));
                g.fillRect(automationRow);
                g.setColour(Colours::white.withAlpha(0.05f));
                g.drawLine(static_cast<float>(automationRow.getX()), static_cast<float>(automationRow.getY()),
                           static_cast<float>(automationRow.getRight()), static_cast<float>(automationRow.getY()));
                paintAutomationLane(g, automationRow, track);
            }
        }

        paintPlayhead(g, gridArea);
    }

    void resized() override
    {
        for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i)
            trackHeaders[static_cast<size_t>(i)]->setBounds(0, trackRowBounds(i).getY(), headerWidth, trackRowBounds(i).getHeight());
    }

    void mouseUp(const MouseEvent& event) override
    {
        if (dragState.active)
        {
            dragState = {};
            return;
        }

        if (event.x < headerWidth || event.y < timelineHeight)
            return;

        const auto trackRow = trackRowAtY(event.y);
        if (trackRow < 0 || trackRow >= static_cast<int>(session.tracks.size()))
            return;

        auto& track = session.tracks[static_cast<size_t>(trackRow)];
        const auto regionRow = trackRegionBounds(trackRow);
        const auto automationRow = trackAutomationBounds(trackRow);

        if (const auto regionIndex = regionIndexAtPoint(regionRow, track, event.getPosition()); regionIndex >= 0)
        {
            if (onRegionSelect != nullptr)
                onRegionSelect(track.id, regionIndex);
            return;
        }

        if (automationRow.contains(event.getPosition()))
        {
            if (onTrackSelect != nullptr)
                onTrackSelect(track.id);
            return;
        }

        if (onTrackSelect != nullptr)
            onTrackSelect(track.id);
    }

    void mouseDown(const MouseEvent& event) override
    {
        dragState = {};
        grabKeyboardFocus();

        if (const auto automationHit = hitTestAutomationPoint(event.getPosition()); automationHit.trackId >= 0)
        {
            if (onTrackSelect != nullptr)
                onTrackSelect(automationHit.trackId);

            selectedAutomationTrackId = automationHit.trackId;
            selectedAutomationPointIndex = automationHit.pointIndex;

            dragState.active = true;
            dragState.mode = DragMode::automationPoint;
            dragState.trackId = automationHit.trackId;
            dragState.automationPointIndex = automationHit.pointIndex;
            dragState.dragStartPoint = event.getPosition();

            if (auto* track = getTrackById(automationHit.trackId))
            {
                const auto& point = getAutomationPoints(*track)[static_cast<size_t>(automationHit.pointIndex)];
                dragState.originalAutomationBeat = point.beat;
                dragState.originalAutomationValue = point.value;
            }

            return;
        }

        if (const auto target = hitTestRegion(event.getPosition()); target.trackId >= 0)
        {
            selectedAutomationTrackId = -1;
            selectedAutomationPointIndex = -1;
            if (onRegionSelect != nullptr)
                onRegionSelect(target.trackId, target.regionIndex);

            if (target.handle != RegionHandle::none)
            {
                dragState.active = true;
                dragState.mode = DragMode::region;
                dragState.handle = target.handle;
                dragState.trackId = target.trackId;
                dragState.regionIndex = target.regionIndex;
                dragState.dragStartPoint = event.getPosition();

                if (auto* region = getRegionBySelection(target.trackId, target.regionIndex))
                {
                    dragState.originalStartBeat = region->startBeat;
                    dragState.originalLengthInBeats = region->lengthInBeats;
                    dragState.originalSourceOffsetSeconds = region->sourceOffsetSeconds;
                    dragState.originalFadeInBeats = region->fadeInBeats;
                    dragState.originalFadeOutBeats = region->fadeOutBeats;
                }
            }
        }
    }

    void mouseDoubleClick(const MouseEvent& event) override
    {
        if (event.x < headerWidth || event.y < timelineHeight)
            return;

        if (const auto automationHit = hitTestAutomationPoint(event.getPosition()); automationHit.trackId >= 0)
        {
            if (auto* track = getTrackById(automationHit.trackId))
            {
                auto& points = getAutomationPoints(*track);
                if (automationHit.pointIndex >= 0 && automationHit.pointIndex < static_cast<int>(points.size()) - 1)
                {
                    auto& point = points[static_cast<size_t>(automationHit.pointIndex)];
                    point.shapeToNext = nextSegmentShape(point.shapeToNext);
                    selectedAutomationTrackId = automationHit.trackId;
                    selectedAutomationPointIndex = automationHit.pointIndex;

                    if (onRegionEdit != nullptr)
                        onRegionEdit();

                    repaint();
                    return;
                }
            }
        }

        const auto trackRow = trackRowAtY(event.y);
        if (trackRow < 0 || trackRow >= static_cast<int>(session.tracks.size()))
            return;

        auto& track = session.tracks[static_cast<size_t>(trackRow)];
        const auto automationRow = trackAutomationBounds(trackRow);

        if (! automationRow.contains(event.getPosition()))
            return;

        if (onTrackSelect != nullptr)
            onTrackSelect(track.id);

        selectedAutomationTrackId = track.id;

        AutomationPoint point;
        point.beat = juce::jlimit(1.0, session.transport.visibleBeats, xPositionToBeat(event.x));
        point.value = yPositionToAutomationValue(automationRow, event.y, track.visibleAutomationLane);
        auto& points = getAutomationPoints(track);
        points.push_back(point);
        sortAutomation(points);
        selectedAutomationPointIndex = indexOfAutomationPoint(track, point.beat, point.value);

        if (onRegionEdit != nullptr)
            onRegionEdit();

        repaint();
    }

    void mouseDrag(const MouseEvent& event) override
    {
        if (! dragState.active)
            return;

        if (dragState.mode == DragMode::automationPoint)
        {
            if (auto* track = getTrackById(dragState.trackId))
            {
                auto& points = getAutomationPoints(*track);
                if (dragState.automationPointIndex >= 0
                    && dragState.automationPointIndex < static_cast<int>(points.size()))
                {
                    const auto trackRow = trackRowForId(dragState.trackId);
                    if (trackRow >= 0)
                    {
                        auto& draggedPoint = points[static_cast<size_t>(dragState.automationPointIndex)];
                        draggedPoint.beat = juce::jlimit(1.0, session.transport.visibleBeats,
                                                         dragState.originalAutomationBeat + snapBeatDelta(xDeltaToBeatDelta(event.getDistanceFromDragStartX())));
                        draggedPoint.value = juce::jlimit(automationMinimumValue(*track), automationMaximumValue(*track),
                                                          dragState.originalAutomationValue - static_cast<float>(event.getDistanceFromDragStartY()) / static_cast<float>(automationLaneHeight - 12));
                        sortAutomation(points);
                        selectedAutomationPointIndex = nearestAutomationPointIndex(*track, draggedPoint.beat, draggedPoint.value);
                    }
                }
            }

            if (onRegionEdit != nullptr)
                onRegionEdit();

            repaint();
            return;
        }

        auto* region = getRegionBySelection(dragState.trackId, dragState.regionIndex);
        if (dragState.mode != DragMode::region || region == nullptr)
            return;

        const auto beatDelta = snapBeatDelta(xDeltaToBeatDelta(event.getDistanceFromDragStartX()));
        constexpr double minimumLengthBeats = 0.25;
        constexpr double minimumStartBeat = 1.0;

        if (dragState.handle == RegionHandle::body)
        {
            region->startBeat = juce::jmax(minimumStartBeat, dragState.originalStartBeat + beatDelta);
        }
        else if (dragState.handle == RegionHandle::left)
        {
            const auto maxTrim = dragState.originalLengthInBeats - minimumLengthBeats;
            const auto appliedDelta = juce::jlimit(-1000.0,
                                                   maxTrim,
                                                   beatDelta);
            region->startBeat = juce::jmax(minimumStartBeat, dragState.originalStartBeat + appliedDelta);

            const auto actualStartDelta = region->startBeat - dragState.originalStartBeat;
            region->lengthInBeats = juce::jmax(minimumLengthBeats, dragState.originalLengthInBeats - actualStartDelta);
            region->sourceOffsetSeconds = juce::jmax(0.0,
                                                     dragState.originalSourceOffsetSeconds
                                                         + actualStartDelta * (60.0 / juce::jmax(1.0, session.transport.bpm)));
        }
        else if (dragState.handle == RegionHandle::right)
        {
            region->lengthInBeats = juce::jmax(minimumLengthBeats, dragState.originalLengthInBeats + beatDelta);
        }
        else if (dragState.handle == RegionHandle::fadeIn)
        {
            const auto maximumFade = juce::jmax(0.0, region->lengthInBeats - minimumLengthBeats);
            region->fadeInBeats = juce::jlimit(0.0, maximumFade, dragState.originalFadeInBeats + beatDelta);
        }
        else if (dragState.handle == RegionHandle::fadeOut)
        {
            const auto maximumFade = juce::jmax(0.0, region->lengthInBeats - minimumLengthBeats);
            region->fadeOutBeats = juce::jlimit(0.0, maximumFade, dragState.originalFadeOutBeats - beatDelta);
        }

        if (onRegionEdit != nullptr)
            onRegionEdit();

        repaint();
    }

    bool keyPressed(const KeyPress& key) override
    {
        if ((key == KeyPress::deleteKey || key == KeyPress::backspaceKey)
            && selectedAutomationTrackId >= 0
            && selectedAutomationPointIndex >= 0)
        {
            if (auto* track = getTrackById(selectedAutomationTrackId))
            {
                auto& points = getAutomationPoints(*track);
                if (selectedAutomationPointIndex < static_cast<int>(points.size()))
                {
                    points.erase(points.begin() + selectedAutomationPointIndex);
                    selectedAutomationPointIndex = -1;

                    if (onRegionEdit != nullptr)
                        onRegionEdit();

                    repaint();
                    return true;
                }
            }
        }

        return false;
    }

private:
    static constexpr int headerWidth = 240;
    static constexpr int timelineHeight = 34;
    static constexpr int regionLaneHeight = 82;
    static constexpr int automationLaneHeight = 38;

    enum class RegionHandle
    {
        none,
        body,
        left,
        right,
        fadeIn,
        fadeOut
    };

    enum class DragMode
    {
        none,
        region,
        automationPoint
    };

    struct RegionHit
    {
        int trackId { -1 };
        int regionIndex { -1 };
        RegionHandle handle { RegionHandle::none };
    };

    struct AutomationHit
    {
        int trackId { -1 };
        int pointIndex { -1 };
    };

    struct DragState
    {
        bool active { false };
        DragMode mode { DragMode::none };
        RegionHandle handle { RegionHandle::none };
        int trackId { -1 };
        int regionIndex { -1 };
        int automationPointIndex { -1 };
        Point<int> dragStartPoint;
        double originalStartBeat { 0.0 };
        double originalLengthInBeats { 0.0 };
        double originalSourceOffsetSeconds { 0.0 };
        double originalFadeInBeats { 0.0 };
        double originalFadeOutBeats { 0.0 };
        double originalAutomationBeat { 1.0 };
        float originalAutomationValue { 1.0f };
    };

    void rebuildTrackHeaders()
    {
        trackHeaders.clear();
        removeAllChildren();

        for (auto& track : session.tracks)
        {
            auto header = std::make_unique<TrackHeaderComponent>(track,
                                                                 onTrackSelect,
                                                                 [this] (int)
                                                                 {
                                                                     if (onAutomationLayoutChange != nullptr)
                                                                         onAutomationLayoutChange();
                                                                 },
                                                                 [this]
                                                                 {
                                                                     if (onTrackEdit != nullptr)
                                                                         onTrackEdit();
                                                                 });
            addAndMakeVisible(*header);
            trackHeaders.push_back(std::move(header));
        }

        resized();
    }

    void paintTimeline(Graphics& g, Rectangle<int> area)
    {
        g.setColour(Colour::fromRGB(33, 39, 50));
        g.fillRect(area);

        auto beatWidth = static_cast<float>(area.getWidth()) / static_cast<float>(session.transport.visibleBeats);

        for (int beat = 0; beat <= static_cast<int>(session.transport.visibleBeats); ++beat)
        {
            auto x = static_cast<float>(area.getX()) + beatWidth * static_cast<float>(beat);
            auto barNumber = (beat / 4) + 1;
            auto isBar = (beat % 4) == 0;

            g.setColour(isBar ? Colours::white.withAlpha(0.18f) : Colours::white.withAlpha(0.06f));
            g.drawLine(x, static_cast<float>(area.getY()), x, static_cast<float>(getHeight()));

            if (beat < static_cast<int>(session.transport.visibleBeats) && isBar)
            {
                g.setColour(Colours::white.withAlpha(0.75f));
                g.drawText(String(barNumber), static_cast<int>(x) + 8, area.getY(), 40, area.getHeight(), Justification::centredLeft, false);
            }
        }
    }

    void paintBarGrid(Graphics& g, Rectangle<int> row)
    {
        auto beatWidth = static_cast<float>(row.getWidth()) / static_cast<float>(session.transport.visibleBeats);

        for (int beat = 0; beat <= static_cast<int>(session.transport.visibleBeats); ++beat)
        {
            auto x = static_cast<float>(row.getX()) + beatWidth * static_cast<float>(beat);
            auto alpha = (beat % 4) == 0 ? 0.14f : 0.05f;
            g.setColour(Colours::white.withAlpha(alpha));
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(row.getY()), static_cast<float>(row.getBottom()));
        }
    }

    void paintAutomationLane(Graphics& g, Rectangle<int> row, const TrackState& track)
    {
        g.setColour(track.selected ? track.colour.withAlpha(0.20f) : Colours::white.withAlpha(0.06f));
        const auto baseline = yForAutomationValue(row,
                                                  track.visibleAutomationLane == AutomationLaneMode::pan ? track.mixer.pan : track.mixer.volume,
                                                  track.visibleAutomationLane);
        g.drawLine(static_cast<float>(row.getX() + 6), baseline,
                   static_cast<float>(row.getRight() - 6), baseline, 1.0f);

        auto points = getAutomationPointsCopy(track);

        if (points.empty())
            points.push_back({ 1.0, track.visibleAutomationLane == AutomationLaneMode::pan ? track.mixer.pan : track.mixer.volume });

        g.setColour(track.colour.withAlpha(track.selected ? 0.95f : 0.70f));
        juce::Path automationPath;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto x = xForBeat(points[i].beat);
            const auto y = yForAutomationValue(row, points[i].value, track.visibleAutomationLane);

            if (i == 0)
                automationPath.startNewSubPath(x, y);

            if (i + 1 < points.size())
            {
                const auto& next = points[i + 1];
                appendAutomationSegment(automationPath, row, points[i], next, track.visibleAutomationLane);

                if (track.selected)
                {
                    const auto segmentMidX = xForBeat((points[i].beat + next.beat) * 0.5);
                    const auto segmentMidValue = interpolateAutomationDisplayValue({ points[i], next }, (points[i].beat + next.beat) * 0.5, points[i].value);
                    const auto segmentMidY = yForAutomationValue(row, segmentMidValue, track.visibleAutomationLane);
                    g.setColour(Colours::white.withAlpha(0.38f));
                    g.drawText(shapeShortLabel(points[i].shapeToNext),
                               juce::Rectangle<int>(static_cast<int>(segmentMidX) - 18, static_cast<int>(segmentMidY) - 18, 36, 12),
                               Justification::centred, false);
                    g.setColour(track.colour.withAlpha(track.selected ? 0.95f : 0.70f));
                }
            }

            const auto pointSelected = track.id == selectedAutomationTrackId && static_cast<int>(i) == selectedAutomationPointIndex;
            g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            if (pointSelected)
            {
                g.setColour(Colours::white.withAlpha(0.30f));
                g.drawEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.0f);

                if (i + 1 < points.size())
                {
                    g.setColour(Colours::white.withAlpha(0.85f));
                    g.drawText(toDisplayString(points[i].shapeToNext),
                               juce::Rectangle<int>(static_cast<int>(x) + 8, static_cast<int>(y) - 16, 72, 14),
                               Justification::centredLeft, false);
                }

                g.setColour(track.colour.withAlpha(track.selected ? 0.95f : 0.70f));
            }
        }

        g.strokePath(automationPath, juce::PathStrokeType(2.0f));
        g.setColour(Colours::white.withAlpha(0.46f));
        g.drawText(toDisplayString(track.visibleAutomationLane) + " automation", row.removeFromLeft(150).reduced(8, 4), Justification::centredLeft, false);
    }

    void paintTrackRegions(Graphics& g, Rectangle<int> row, const TrackState& track)
    {
        for (size_t regionIndex = 0; regionIndex < track.regions.size(); ++regionIndex)
        {
            const auto& region = track.regions[regionIndex];
            auto regionBounds = boundsForRegion(row, region);
            const auto regionSelected = track.id == session.selectedRegionTrackId
                && static_cast<int>(regionIndex) == session.selectedRegionIndex;

            auto brightness = track.selected ? 0.94f : 0.78f;
            if (region.kind == RegionKind::generated)
                brightness = jmin(1.0f, brightness + 0.08f);
            if (regionSelected)
                brightness = jmin(1.0f, brightness + 0.12f);

            g.setColour(region.colour.withSaturation(0.65f).withBrightness(brightness));
            g.fillRoundedRectangle(regionBounds.toFloat(), 8.0f);

            if (regionSelected)
            {
                g.setColour(Colours::white.withAlpha(0.55f));
                g.drawRoundedRectangle(regionBounds.toFloat().reduced(1.0f), 8.0f, 2.0f);

                auto leftHandle = Rectangle<int>(regionBounds.getX(), regionBounds.getY(), 8, regionBounds.getHeight()).reduced(1, 8);
                auto rightHandle = Rectangle<int>(regionBounds.getRight() - 8, regionBounds.getY(), 8, regionBounds.getHeight()).reduced(1, 8);
                g.setColour(Colours::white.withAlpha(0.42f));
                g.fillRoundedRectangle(leftHandle.toFloat(), 3.0f);
                g.fillRoundedRectangle(rightHandle.toFloat(), 3.0f);

                if (region.kind == RegionKind::audio)
                {
                    const auto fadeInWidth = beatDeltaToPixels(region.fadeInBeats);
                    const auto fadeOutWidth = beatDeltaToPixels(region.fadeOutBeats);

                    g.setColour(Colours::white.withAlpha(0.58f));
                    g.drawLine(static_cast<float>(regionBounds.getX() + 6),
                               static_cast<float>(regionBounds.getBottom() - 8),
                               static_cast<float>(regionBounds.getX() + 6 + fadeInWidth),
                               static_cast<float>(regionBounds.getY() + 8),
                               1.5f);
                    g.drawLine(static_cast<float>(regionBounds.getRight() - 6 - fadeOutWidth),
                               static_cast<float>(regionBounds.getY() + 8),
                               static_cast<float>(regionBounds.getRight() - 6),
                               static_cast<float>(regionBounds.getBottom() - 8),
                               1.5f);

                    auto fadeInHandle = Rectangle<int>(regionBounds.getX() + 4 + fadeInWidth, regionBounds.getY() + 4, 8, 10);
                    auto fadeOutHandle = Rectangle<int>(regionBounds.getRight() - 12 - fadeOutWidth, regionBounds.getY() + 4, 8, 10);
                    g.fillRoundedRectangle(fadeInHandle.toFloat(), 3.0f);
                    g.fillRoundedRectangle(fadeOutHandle.toFloat(), 3.0f);
                }
            }

            auto waveformBounds = regionBounds.reduced(10, 10);
            auto drewWaveform = false;

            if (region.kind == RegionKind::audio && region.sourceFilePath.isNotEmpty())
            {
                if (auto* thumbnail = getThumbnailForFile(region.sourceFilePath))
                {
                    const auto secondsPerBeat = 60.0 / juce::jmax(1.0, session.transport.bpm);
                    const auto regionDurationSeconds = juce::jmax(0.01, region.lengthInBeats * secondsPerBeat);
                    const auto thumbnailStartSeconds = juce::jmax(0.0, region.sourceOffsetSeconds);
                    const auto thumbnailEndSeconds = juce::jmin(thumbnail->getTotalLength(),
                                                                thumbnailStartSeconds + regionDurationSeconds);

                    g.setColour(Colours::white.withAlpha(regionSelected ? 0.62f : 0.34f));
                    thumbnail->drawChannels(g,
                                            waveformBounds,
                                            thumbnailStartSeconds,
                                            juce::jmax(thumbnailStartSeconds + 0.001, thumbnailEndSeconds),
                                            1.0f);
                    drewWaveform = thumbnail->getTotalLength() > 0.0;
                }
            }

            if (! drewWaveform)
            {
                g.setColour(Colours::white.withAlpha(0.12f));
                for (int stripe = 0; stripe < 5; ++stripe)
                {
                    auto lineY = regionBounds.getY() + 10 + stripe * 9;
                    g.drawLine(static_cast<float>(regionBounds.getX() + 10), static_cast<float>(lineY),
                               static_cast<float>(regionBounds.getRight() - 10), static_cast<float>(lineY + ((stripe % 2 == 0) ? 4 : -4)), 1.1f);
                }
            }

            g.setColour(Colours::white.withAlpha(0.92f));
            g.drawText(region.name, regionBounds.reduced(10, 8), Justification::topLeft, false);
            g.setColour(Colours::white.withAlpha(0.62f));
            auto footer = regionBounds.removeFromBottom(18).reduced(10, 0);
            auto footerText = describeRegionKind(region.kind);
            if (region.kind == RegionKind::audio && region.sourceFilePath.isNotEmpty())
                footerText += " | clip linked";
            if (region.kind == RegionKind::audio)
                footerText += " | gain " + String(region.gain, 2) + "x";
            g.drawText(footerText, footer, Justification::centredLeft, false);
        }
    }

    juce::AudioThumbnail* getThumbnailForFile(const juce::String& filePath)
    {
        if (filePath.isEmpty())
            return nullptr;

        if (const auto it = waveformCache.find(filePath); it != waveformCache.end())
            return it->second.get();

        auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, audioFormatManager, thumbnailCache);
        auto source = std::make_unique<juce::FileInputSource>(juce::File(filePath));

        if (! thumbnail->setSource(source.release()))
            return nullptr;

        auto* rawThumbnail = thumbnail.get();
        waveformCache[filePath] = std::move(thumbnail);
        return rawThumbnail;
    }

    int regionIndexAtPoint(Rectangle<int> row, const TrackState& track, Point<int> point) const
    {
        for (size_t regionIndex = 0; regionIndex < track.regions.size(); ++regionIndex)
        {
            const auto& region = track.regions[regionIndex];
            auto regionBounds = boundsForRegion(row, region);

            if (regionBounds.contains(point))
                return static_cast<int>(regionIndex);
        }

        return -1;
    }

    RegionHandle handleAtPoint(Rectangle<int> row, const TrackState& track, int regionIndex, Point<int> point) const
    {
        const auto& region = track.regions[static_cast<size_t>(regionIndex)];
        const auto regionBounds = boundsForRegion(row, region);
        const auto leftZone = Rectangle<int>(regionBounds.getX(), regionBounds.getY(), 10, regionBounds.getHeight());
        const auto rightZone = Rectangle<int>(regionBounds.getRight() - 10, regionBounds.getY(), 10, regionBounds.getHeight());

        if (leftZone.contains(point) && region.kind == RegionKind::audio)
            return RegionHandle::left;
        if (rightZone.contains(point) && region.kind == RegionKind::audio)
            return RegionHandle::right;

        if (region.kind == RegionKind::audio)
        {
            const auto fadeInWidth = beatDeltaToPixels(region.fadeInBeats);
            const auto fadeOutWidth = beatDeltaToPixels(region.fadeOutBeats);
            const auto fadeInZone = Rectangle<int>(regionBounds.getX() + 2 + fadeInWidth, regionBounds.getY(), 12, 16);
            const auto fadeOutZone = Rectangle<int>(regionBounds.getRight() - 14 - fadeOutWidth, regionBounds.getY(), 12, 16);

            if (fadeInZone.contains(point))
                return RegionHandle::fadeIn;
            if (fadeOutZone.contains(point))
                return RegionHandle::fadeOut;
        }

        return RegionHandle::body;
    }

    void paintPlayhead(Graphics& g, Rectangle<int> gridArea)
    {
        auto beatWidth = static_cast<float>(gridArea.getWidth()) / static_cast<float>(session.transport.visibleBeats);
        auto x = static_cast<float>(gridArea.getX()) + static_cast<float>((session.transport.playheadBeat - 1.0) * beatWidth);
        g.setColour(Colour::fromRGB(255, 107, 72));
        g.drawLine(x, 0.0f, x, static_cast<float>(getHeight()), 2.0f);
        g.fillEllipse(x - 5.0f, 8.0f, 10.0f, 10.0f);
    }

    Rectangle<int> boundsForRegion(Rectangle<int> row, const Region& region) const
    {
        const auto beatWidth = static_cast<float>(row.getWidth()) / static_cast<float>(session.transport.visibleBeats);
        const auto x = row.getX() + static_cast<int>((region.startBeat - 1.0) * beatWidth);
        const auto w = static_cast<int>(region.lengthInBeats * beatWidth);
        return Rectangle<int>(x + 3, row.getY() + 10, jmax(40, w - 6), row.getHeight() - 20);
    }

    double xDeltaToBeatDelta(int xDelta) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - headerWidth);
        return static_cast<double>(xDelta) * (session.transport.visibleBeats / static_cast<double>(gridWidth));
    }

    double snapBeatDelta(double beatDelta) const
    {
        constexpr double snapResolutionBeats = 0.25;
        return std::round(beatDelta / snapResolutionBeats) * snapResolutionBeats;
    }

    int beatDeltaToPixels(double beats) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - headerWidth);
        return static_cast<int>(std::round(beats * (static_cast<double>(gridWidth) / session.transport.visibleBeats)));
    }

    int trackAutomationHeight(const TrackState& track) const
    {
        return (track.visibleAutomationLane == AutomationLaneMode::none || ! track.automationExpanded) ? 0 : automationLaneHeight;
    }

    int trackTopForRow(int trackRow) const
    {
        auto y = timelineHeight;
        for (int i = 0; i < trackRow; ++i)
            y += regionLaneHeight + trackAutomationHeight(session.tracks[static_cast<size_t>(i)]);
        return y;
    }

    Rectangle<int> trackRowBounds(int trackRow) const
    {
        return Rectangle<int>(headerWidth,
                              trackTopForRow(trackRow),
                              getWidth() - headerWidth,
                              regionLaneHeight + trackAutomationHeight(session.tracks[static_cast<size_t>(trackRow)]));
    }

    Rectangle<int> trackRegionBounds(int trackRow) const
    {
        return trackRowBounds(trackRow).removeFromTop(regionLaneHeight);
    }

    Rectangle<int> trackAutomationBounds(int trackRow) const
    {
        auto row = trackRowBounds(trackRow);
        row.removeFromTop(regionLaneHeight);
        return row;
    }

    float xForBeat(double beat) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - headerWidth);
        return static_cast<float>(headerWidth) + static_cast<float>(((beat - 1.0) / session.transport.visibleBeats) * gridWidth);
    }

    double xPositionToBeat(int x) const
    {
        return 1.0 + xDeltaToBeatDelta(x - headerWidth);
    }

    float yForAutomationValue(Rectangle<int> row, float value, AutomationLaneMode mode) const
    {
        const auto top = static_cast<float>(row.getY() + 8);
        const auto bottom = static_cast<float>(row.getBottom() - 8);
        if (mode == AutomationLaneMode::pan)
            return juce::jmap(juce::jlimit(-1.0f, 1.0f, value), 1.0f, -1.0f, top, bottom);

        return juce::jmap(juce::jlimit(0.0f, 1.0f, value), 1.0f, 0.0f, top, bottom);
    }

    float yPositionToAutomationValue(Rectangle<int> row, int y, AutomationLaneMode mode) const
    {
        const auto top = static_cast<float>(row.getY() + 8);
        const auto bottom = static_cast<float>(row.getBottom() - 8);
        if (mode == AutomationLaneMode::pan)
            return juce::jlimit(-1.0f, 1.0f, juce::jmap(static_cast<float>(y), bottom, top, -1.0f, 1.0f));

        return juce::jlimit(0.0f, 1.0f, juce::jmap(static_cast<float>(y), bottom, top, 0.0f, 1.0f));
    }

    void sortAutomation(std::vector<AutomationPoint>& points) const
    {
        std::sort(points.begin(), points.end(), [] (const auto& left, const auto& right) { return left.beat < right.beat; });
    }

    void appendAutomationSegment(juce::Path& path,
                                 Rectangle<int> row,
                                 const AutomationPoint& left,
                                 const AutomationPoint& right,
                                 AutomationLaneMode mode) const
    {
        constexpr int segmentResolution = 16;

        for (int step = 1; step <= segmentResolution; ++step)
        {
            const auto t = static_cast<float>(step) / static_cast<float>(segmentResolution);
            const auto beat = juce::jmap(t, static_cast<float>(left.beat), static_cast<float>(right.beat));
            const auto value = interpolateAutomationDisplayValue({ left, right }, beat, left.value);
            path.lineTo(xForBeat(beat), yForAutomationValue(row, value, mode));
        }
    }

    AutomationPoint::SegmentShape nextSegmentShape(AutomationPoint::SegmentShape shape) const
    {
        switch (shape)
        {
            case AutomationPoint::SegmentShape::linear: return AutomationPoint::SegmentShape::easeIn;
            case AutomationPoint::SegmentShape::easeIn: return AutomationPoint::SegmentShape::easeOut;
            case AutomationPoint::SegmentShape::easeOut: return AutomationPoint::SegmentShape::step;
            case AutomationPoint::SegmentShape::step: return AutomationPoint::SegmentShape::linear;
        }

        return AutomationPoint::SegmentShape::linear;
    }

    juce::String shapeShortLabel(AutomationPoint::SegmentShape shape) const
    {
        switch (shape)
        {
            case AutomationPoint::SegmentShape::linear: return "LIN";
            case AutomationPoint::SegmentShape::easeIn: return "IN";
            case AutomationPoint::SegmentShape::easeOut: return "OUT";
            case AutomationPoint::SegmentShape::step: return "STEP";
        }

        return "LIN";
    }

    AutomationHit hitTestAutomationPoint(Point<int> point) const
    {
        if (point.x < headerWidth || point.y < timelineHeight)
            return {};

        const auto trackRow = trackRowAtY(point.y);
        if (trackRow < 0 || trackRow >= static_cast<int>(session.tracks.size()))
            return {};

        const auto& track = session.tracks[static_cast<size_t>(trackRow)];
        const auto automationRow = trackAutomationBounds(trackRow);

        if (! automationRow.contains(point))
            return {};

        const auto& points = getAutomationPoints(track);
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto pointX = xForBeat(points[i].beat);
            const auto pointY = yForAutomationValue(automationRow, points[i].value, track.visibleAutomationLane);
            juce::Rectangle<float> pointBounds(pointX - 7.0f, pointY - 7.0f, 14.0f, 14.0f);

            if (pointBounds.contains(static_cast<float>(point.x), static_cast<float>(point.y)))
                return { track.id, static_cast<int>(i) };
        }

        return {};
    }

    RegionHit hitTestRegion(Point<int> point) const
    {
        if (point.x < headerWidth || point.y < timelineHeight)
            return {};

        const auto trackRow = trackRowAtY(point.y);
        if (trackRow < 0 || trackRow >= static_cast<int>(session.tracks.size()))
            return {};

        const auto& track = session.tracks[static_cast<size_t>(trackRow)];
        const auto row = trackRegionBounds(trackRow);

        for (size_t regionIndex = 0; regionIndex < track.regions.size(); ++regionIndex)
        {
            const auto regionBounds = boundsForRegion(row, track.regions[regionIndex]);

            if (! regionBounds.contains(point))
                continue;

            return { track.id, static_cast<int>(regionIndex), handleAtPoint(row, track, static_cast<int>(regionIndex), point) };
        }

        return {};
    }

    Region* getRegionBySelection(int trackId, int regionIndex)
    {
        auto* track = getTrackById(trackId);

        if (track == nullptr)
            return nullptr;

        if (regionIndex < 0 || regionIndex >= static_cast<int>(track->regions.size()))
            return nullptr;

        return &track->regions[static_cast<size_t>(regionIndex)];
    }

    TrackState* getTrackById(int trackId)
    {
        auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [trackId] (const auto& track) {
            return track.id == trackId;
        });

        return trackIt != session.tracks.end() ? &(*trackIt) : nullptr;
    }

    int trackRowForId(int trackId) const
    {
        for (size_t i = 0; i < session.tracks.size(); ++i)
            if (session.tracks[i].id == trackId)
                return static_cast<int>(i);

        return -1;
    }

    int trackRowAtY(int y) const
    {
        auto currentTop = timelineHeight;

        for (size_t i = 0; i < session.tracks.size(); ++i)
        {
            const auto height = regionLaneHeight + trackAutomationHeight(session.tracks[i]);
            if (y >= currentTop && y < currentTop + height)
                return static_cast<int>(i);
            currentTop += height;
        }

        return -1;
    }

    std::vector<AutomationPoint>& getAutomationPoints(TrackState& track)
    {
        return track.visibleAutomationLane == AutomationLaneMode::pan ? track.panAutomation : track.volumeAutomation;
    }

    const std::vector<AutomationPoint>& getAutomationPoints(const TrackState& track) const
    {
        return track.visibleAutomationLane == AutomationLaneMode::pan ? track.panAutomation : track.volumeAutomation;
    }

    std::vector<AutomationPoint> getAutomationPointsCopy(const TrackState& track) const
    {
        return getAutomationPoints(track);
    }

    float automationMinimumValue(const TrackState& track) const
    {
        return track.visibleAutomationLane == AutomationLaneMode::pan ? -1.0f : 0.0f;
    }

    float automationMaximumValue(const TrackState&) const
    {
        return 1.0f;
    }

    int nearestAutomationPointIndex(const TrackState& track, double beat, float value) const
    {
        const auto& points = getAutomationPoints(track);
        for (size_t i = 0; i < points.size(); ++i)
            if (std::abs(points[i].beat - beat) < 0.001 && std::abs(points[i].value - value) < 0.001f)
                return static_cast<int>(i);
        return -1;
    }

    int indexOfAutomationPoint(const TrackState& track, double beat, float value) const
    {
        return nearestAutomationPointIndex(track, beat, value);
    }

    SessionState& session;
    std::function<void(int)> onTrackSelect;
    std::function<void(int, int)> onRegionSelect;
    std::function<void()> onRegionEdit;
    std::function<void()> onAutomationLayoutChange;
    std::function<void()> onTrackEdit;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> waveformCache;
    std::vector<std::unique_ptr<TrackHeaderComponent>> trackHeaders;
    DragState dragState;
    int selectedAutomationTrackId { -1 };
    int selectedAutomationPointIndex { -1 };
};

class MainComponent::PianoRollComponent final : public Component
{
public:
    PianoRollComponent(SessionState& sessionToUse, std::function<void()> onEditToUse)
        : session(sessionToUse), onEdit(std::move(onEditToUse))
    {
        setWantsKeyboardFocus(true);
    }

    void refresh()
    {
        repaint();
    }

    bool hasEditableMidiRegion() const
    {
        const auto* region = session.getSelectedRegion();
        return region != nullptr && (region->kind == RegionKind::midi || region->kind == RegionKind::generated);
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour::fromRGB(18, 21, 27));

        auto* region = session.getSelectedRegion();
        if (region == nullptr || ! hasEditableMidiRegion())
        {
            paintEmptyState(g, "Select a MIDI region to edit notes.");
            return;
        }

        auto area = getLocalBounds().reduced(10);
        auto header = area.removeFromTop(28);
        paintHeader(g, header, *region);

        if (area.getWidth() < pianoWidth + 160 || area.getHeight() < 120)
        {
            paintEmptyState(g, "Piano roll needs a little more space.");
            return;
        }

        auto pianoArea = area.removeFromLeft(pianoWidth);
        auto gridArea = area;

        paintPitchKeyboard(g, pianoArea);
        paintGrid(g, gridArea, *region);
        paintNotes(g, gridArea, *region);
    }

    void mouseDown(const MouseEvent& event) override
    {
        dragState = {};
        grabKeyboardFocus();

        auto* region = session.getSelectedRegion();
        if (region == nullptr || ! hasEditableMidiRegion())
            return;

        const auto layout = layoutForCurrentRegion();
        if (! layout.gridArea.contains(event.getPosition()))
            return;

        const auto hit = hitTestNote(*region, layout.gridArea, event.getPosition());
        clearNoteSelection(*region);

        if (hit.noteIndex >= 0)
        {
            auto& note = region->midiNotes[static_cast<size_t>(hit.noteIndex)];
            note.selected = true;

            dragState.active = true;
            dragState.noteIndex = hit.noteIndex;
            dragState.mode = hit.handle;
            dragState.dragStart = event.getPosition();
            dragState.originalStartBeat = note.startBeat;
            dragState.originalLengthBeats = note.lengthInBeats;
            dragState.originalPitch = note.pitch;
            repaint();
            return;
        }

        repaint();
    }

    void mouseDrag(const MouseEvent& event) override
    {
        if (! dragState.active)
            return;

        auto* region = session.getSelectedRegion();
        if (region == nullptr || ! hasEditableMidiRegion())
            return;

        if (dragState.noteIndex < 0 || dragState.noteIndex >= static_cast<int>(region->midiNotes.size()))
            return;

        const auto layout = layoutForCurrentRegion();
        auto& note = region->midiNotes[static_cast<size_t>(dragState.noteIndex)];
        const auto beatDelta = snapBeat(static_cast<float>(event.getPosition().x - dragState.dragStart.x) / layout.beatWidth);
        const auto pitchDelta = -(event.getPosition().y - dragState.dragStart.y) / noteHeight;

        if (dragState.mode == NoteHandle::body)
        {
            note.startBeat = juce::jlimit(0.0, juce::jmax(0.0, region->lengthInBeats - minimumNoteLength), dragState.originalStartBeat + beatDelta);
            note.pitch = juce::jlimit(minPitch, maxPitch, dragState.originalPitch + pitchDelta);
        }
        else if (dragState.mode == NoteHandle::left)
        {
            const auto desiredStart = juce::jlimit(0.0,
                                                   dragState.originalStartBeat + dragState.originalLengthBeats - minimumNoteLength,
                                                   dragState.originalStartBeat + beatDelta);
            const auto endBeat = dragState.originalStartBeat + dragState.originalLengthBeats;
            note.startBeat = desiredStart;
            note.lengthInBeats = juce::jmax(minimumNoteLength, endBeat - desiredStart);
        }
        else if (dragState.mode == NoteHandle::right)
        {
            note.lengthInBeats = juce::jmax(minimumNoteLength,
                                            snapBeat(dragState.originalLengthBeats + beatDelta));
            note.lengthInBeats = juce::jmin(note.lengthInBeats, juce::jmax(minimumNoteLength, region->lengthInBeats - note.startBeat));
        }

        if (onEdit != nullptr)
            onEdit();

        repaint();
    }

    void mouseUp(const MouseEvent&) override
    {
        dragState = {};
    }

    void mouseDoubleClick(const MouseEvent& event) override
    {
        auto* region = session.getSelectedRegion();
        if (region == nullptr || ! hasEditableMidiRegion())
            return;

        const auto layout = layoutForCurrentRegion();
        if (! layout.gridArea.contains(event.getPosition()))
            return;

        const auto hit = hitTestNote(*region, layout.gridArea, event.getPosition());
        if (hit.noteIndex >= 0)
            return;

        clearNoteSelection(*region);

        Region::MidiNote note;
        note.pitch = pitchForY(layout.gridArea, event.y);
        note.startBeat = snapBeat(beatForX(layout.gridArea, event.x));
        note.lengthInBeats = 1.0;
        note.velocity = 100;
        note.selected = true;
        note.lengthInBeats = juce::jmin(note.lengthInBeats, juce::jmax(minimumNoteLength, region->lengthInBeats - note.startBeat));

        region->midiNotes.push_back(note);

        if (onEdit != nullptr)
            onEdit();

        repaint();
    }

    bool keyPressed(const KeyPress& key) override
    {
        if (key != KeyPress::deleteKey && key != KeyPress::backspaceKey)
            return false;

        auto* region = session.getSelectedRegion();
        if (region == nullptr || ! hasEditableMidiRegion())
            return false;

        const auto beforeSize = region->midiNotes.size();
        region->midiNotes.erase(std::remove_if(region->midiNotes.begin(),
                                               region->midiNotes.end(),
                                               [] (const auto& note) { return note.selected; }),
                                region->midiNotes.end());

        if (region->midiNotes.size() == beforeSize)
            return false;

        if (onEdit != nullptr)
            onEdit();

        repaint();
        return true;
    }

private:
    static constexpr int pianoWidth = 74;
    static constexpr int noteHeight = 18;
    static constexpr int minPitch = 36;
    static constexpr int maxPitch = 84;
    static constexpr double minimumNoteLength = 0.25;

    enum class NoteHandle
    {
        none,
        body,
        left,
        right
    };

    struct NoteHit
    {
        int noteIndex { -1 };
        NoteHandle handle { NoteHandle::none };
    };

    struct Layout
    {
        Rectangle<int> pianoArea;
        Rectangle<int> gridArea;
        float beatWidth { 32.0f };
    };

    struct DragState
    {
        bool active { false };
        int noteIndex { -1 };
        NoteHandle mode { NoteHandle::none };
        Point<int> dragStart;
        double originalStartBeat { 0.0 };
        double originalLengthBeats { 1.0 };
        int originalPitch { 60 };
    };

    void paintEmptyState(Graphics& g, const String& message) const
    {
        g.setColour(Colours::white.withAlpha(0.10f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.5f), 12.0f, 1.0f);
        g.setColour(Colours::white.withAlpha(0.56f));
        g.setFont(FontOptions(15.0f, Font::bold));
        g.drawFittedText(message, getLocalBounds().reduced(20), Justification::centred, 2);
    }

    void paintHeader(Graphics& g, Rectangle<int> header, const Region& region) const
    {
        g.setColour(Colours::white.withAlpha(0.92f));
        g.setFont(FontOptions(15.0f, Font::bold));
        g.drawText("Piano Roll", header.removeFromLeft(120), Justification::centredLeft, false);

        g.setColour(Colours::white.withAlpha(0.58f));
        g.setFont(FontOptions(13.0f));
        g.drawText(region.name + " / " + String(region.midiNotes.size()) + " notes",
                   header, Justification::centredLeft, false);
    }

    void paintPitchKeyboard(Graphics& g, Rectangle<int> area) const
    {
        for (int pitch = maxPitch; pitch >= minPitch; --pitch)
        {
            const auto row = noteRowBounds(area, pitch);
            const auto isBlack = isBlackKey(pitch);
            g.setColour(isBlack ? Colour::fromRGB(28, 31, 38) : Colour::fromRGB(39, 43, 52));
            g.fillRect(row);
            g.setColour(Colours::white.withAlpha(0.08f));
            g.drawRect(row);

            if (! isBlack)
            {
                g.setColour(Colours::white.withAlpha(0.70f));
                g.setFont(FontOptions(11.0f, Font::bold));
                g.drawText(midiPitchName(pitch), row.reduced(8, 0), Justification::centredLeft, false);
            }
        }
    }

    void paintGrid(Graphics& g, Rectangle<int> area, const Region& region) const
    {
        for (int pitch = maxPitch; pitch >= minPitch; --pitch)
        {
            const auto row = noteRowBounds(area, pitch);
            g.setColour(isBlackKey(pitch) ? Colour::fromRGB(23, 26, 33) : Colour::fromRGB(27, 31, 39));
            g.fillRect(row);
            g.setColour(Colours::white.withAlpha(0.05f));
            g.drawHorizontalLine(row.getBottom() - 1, static_cast<float>(row.getX()), static_cast<float>(row.getRight()));
        }

        const auto beatWidth = static_cast<float>(area.getWidth()) / static_cast<float>(juce::jmax(1.0, region.lengthInBeats));
        const auto beatCount = static_cast<int>(std::ceil(region.lengthInBeats));

        for (int beat = 0; beat <= beatCount; ++beat)
        {
            const auto x = area.getX() + static_cast<int>(std::round(static_cast<float>(beat) * beatWidth));
            const auto isBar = (beat % 4) == 0;
            g.setColour(Colours::white.withAlpha(isBar ? 0.14f : 0.06f));
            g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));

            if (beat < beatCount)
            {
                for (int subdivision = 1; subdivision < 4; ++subdivision)
                {
                    const auto subX = x + static_cast<int>(std::round(beatWidth * (static_cast<float>(subdivision) / 4.0f)));
                    g.setColour(Colours::white.withAlpha(0.03f));
                    g.drawVerticalLine(subX, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
                }
            }
        }
    }

    void paintNotes(Graphics& g, Rectangle<int> area, const Region& region) const
    {
        for (const auto& note : region.midiNotes)
        {
            auto bounds = boundsForNote(area, region, note);
            const auto fill = note.selected ? Colour::fromRGB(255, 148, 92) : Colour::fromRGB(247, 184, 68);
            g.setColour(fill.withAlpha(note.selected ? 0.96f : 0.82f));
            g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
            g.setColour(Colours::white.withAlpha(0.24f));
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 5.0f, 1.0f);

            if (bounds.getWidth() > 28)
            {
                g.setColour(Colours::white.withAlpha(0.78f));
                g.setFont(FontOptions(10.0f, Font::bold));
                g.drawText(midiPitchName(note.pitch), bounds.reduced(6, 2), Justification::centredLeft, false);
            }
        }
    }

    Layout layoutForCurrentRegion() const
    {
        auto area = getLocalBounds().reduced(10);
        area.removeFromTop(28);
        area.removeFromTop(6);

        Layout layout;
        layout.pianoArea = area.removeFromLeft(pianoWidth);
        layout.gridArea = area;

        if (const auto* region = session.getSelectedRegion())
            layout.beatWidth = static_cast<float>(layout.gridArea.getWidth()) / static_cast<float>(juce::jmax(1.0, region->lengthInBeats));

        return layout;
    }

    Rectangle<int> boundsForNote(Rectangle<int> gridArea, const Region& region, const Region::MidiNote& note) const
    {
        const auto beatWidth = static_cast<float>(gridArea.getWidth()) / static_cast<float>(juce::jmax(1.0, region.lengthInBeats));
        const auto relativeStartBeat = note.startBeat;
        const auto x = gridArea.getX() + static_cast<int>(std::round(static_cast<float>(relativeStartBeat) * beatWidth));
        const auto width = juce::jmax(18, static_cast<int>(std::round(static_cast<float>(note.lengthInBeats) * beatWidth)));
        const auto y = gridArea.getY() + (maxPitch - note.pitch) * noteHeight + 1;
        return { x, y, width, noteHeight - 2 };
    }

    Rectangle<int> noteRowBounds(Rectangle<int> area, int pitch) const
    {
        return { area.getX(), area.getY() + (maxPitch - pitch) * noteHeight, area.getWidth(), noteHeight };
    }

    NoteHit hitTestNote(const Region& region, Rectangle<int> gridArea, Point<int> point) const
    {
        for (int i = static_cast<int>(region.midiNotes.size()) - 1; i >= 0; --i)
        {
            const auto bounds = boundsForNote(gridArea, region, region.midiNotes[static_cast<size_t>(i)]);
            if (! bounds.contains(point))
                continue;

            const auto leftZone = Rectangle<int>(bounds.getX(), bounds.getY(), 8, bounds.getHeight());
            const auto rightZone = Rectangle<int>(bounds.getRight() - 8, bounds.getY(), 8, bounds.getHeight());

            if (leftZone.contains(point))
                return { i, NoteHandle::left };
            if (rightZone.contains(point))
                return { i, NoteHandle::right };
            return { i, NoteHandle::body };
        }

        return {};
    }

    int pitchForY(Rectangle<int> gridArea, int y) const
    {
        const auto clampedY = juce::jlimit(gridArea.getY(), gridArea.getBottom() - 1, y);
        const auto row = (clampedY - gridArea.getY()) / noteHeight;
        return juce::jlimit(minPitch, maxPitch, maxPitch - row);
    }

    double beatForX(Rectangle<int> gridArea, int x) const
    {
        const auto clampedX = juce::jlimit(gridArea.getX(), gridArea.getRight(), x);
        const auto normalised = static_cast<double>(clampedX - gridArea.getX()) / static_cast<double>(juce::jmax(1, gridArea.getWidth()));
        if (const auto* region = session.getSelectedRegion())
            return juce::jlimit(0.0, juce::jmax(0.0, region->lengthInBeats - minimumNoteLength), normalised * region->lengthInBeats);
        return 0.0;
    }

    double snapBeat(double beat) const
    {
        constexpr double snap = 0.25;
        return std::round(beat / snap) * snap;
    }

    void clearNoteSelection(Region& region) const
    {
        for (auto& note : region.midiNotes)
            note.selected = false;
    }

    bool isBlackKey(int pitch) const
    {
        switch (pitch % 12)
        {
            case 1:
            case 3:
            case 6:
            case 8:
            case 10:
                return true;
            default:
                return false;
        }
    }

    String midiPitchName(int pitch) const
    {
        static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto octave = (pitch / 12) - 1;
        return String(names[pitch % 12]) + String(octave);
    }

    SessionState& session;
    std::function<void()> onEdit;
    DragState dragState;
};

class MainComponent::InspectorComponent final : public Component
{
public:
    InspectorComponent(SessionState& sessionToUse,
                       const SuperColliderBridge& bridgeToUse,
                       AudioEngine& audioEngineToUse,
                       std::function<void()> onAssignAudioFileToUse,
                       std::function<void()> onClearAudioFileToUse,
                       std::function<void(int)> onLoadAudioUnitToUse,
                       std::function<void(int)> onOpenAudioUnitToUse,
                       std::function<void(int)> onClearAudioUnitToUse,
                       std::function<void()> onAutomationLayoutChangeToUse,
                       std::function<void()> onAutomationEditedToUse)
        : session(sessionToUse),
          superColliderBridge(bridgeToUse),
          audioEngine(audioEngineToUse),
          onAssignAudioFile(std::move(onAssignAudioFileToUse)),
          onClearAudioFile(std::move(onClearAudioFileToUse)),
          onLoadAudioUnit(std::move(onLoadAudioUnitToUse)),
          onOpenAudioUnit(std::move(onOpenAudioUnitToUse)),
          onClearAudioUnit(std::move(onClearAudioUnitToUse)),
          onAutomationLayoutChange(std::move(onAutomationLayoutChangeToUse)),
          onAutomationEdited(std::move(onAutomationEditedToUse))
    {
        titleLabel.setText("Inspector", dontSendNotification);
        titleLabel.setFont(FontOptions(18.0f, Font::bold));
        trackNameLabel.setFont(FontOptions(21.0f, Font::bold));
        roleLabel.setFont(FontOptions(14.0f));
        audioRegionLabel.setFont(FontOptions(15.0f, Font::bold));
        audioFileLabel.setFont(FontOptions(13.0f));
        regionGainLabel.setFont(FontOptions(13.0f, Font::bold));
        volumeLabel.setFont(FontOptions(12.0f, Font::bold));
        panLabel.setFont(FontOptions(12.0f, Font::bold));
        automationModeLabel.setFont(FontOptions(12.0f, Font::bold));
        automationValueLabel.setFont(FontOptions(12.0f));
        automationWriteLabel.setFont(FontOptions(12.0f, Font::bold));
        superColliderLabel.setFont(FontOptions(13.0f));
        processorLabel.setFont(FontOptions(13.0f));
        slotOneLabel.setFont(FontOptions(12.0f));
        slotTwoLabel.setFont(FontOptions(12.0f));

        addAndMakeVisible(titleLabel);
        addAndMakeVisible(trackNameLabel);
        addAndMakeVisible(roleLabel);
        addAndMakeVisible(audioRegionLabel);
        addAndMakeVisible(audioFileLabel);
        addAndMakeVisible(regionGainLabel);
        addAndMakeVisible(regionGainSlider);
        addAndMakeVisible(volumeLabel);
        addAndMakeVisible(panLabel);
        addAndMakeVisible(automationModeLabel);
        addAndMakeVisible(automationValueLabel);
        addAndMakeVisible(automationWriteLabel);
        addAndMakeVisible(assignAudioButton);
        addAndMakeVisible(clearAudioButton);
        addAndMakeVisible(showVolumeAutomationButton);
        addAndMakeVisible(showPanAutomationButton);
        addAndMakeVisible(hideAutomationButton);
        addAndMakeVisible(readAutomationButton);
        addAndMakeVisible(touchAutomationButton);
        addAndMakeVisible(latchAutomationButton);
        addAndMakeVisible(superColliderLabel);
        addAndMakeVisible(processorLabel);
        addAndMakeVisible(slotOneLabel);
        addAndMakeVisible(slotTwoLabel);
        addAndMakeVisible(loadSlotOneButton);
        addAndMakeVisible(openSlotOneButton);
        addAndMakeVisible(clearSlotOneButton);
        addAndMakeVisible(loadSlotTwoButton);
        addAndMakeVisible(openSlotTwoButton);
        addAndMakeVisible(clearSlotTwoButton);
        addAndMakeVisible(volumeSlider);
        addAndMakeVisible(panSlider);

        volumeLabel.setText("Volume", dontSendNotification);
        panLabel.setText("Pan", dontSendNotification);
        automationModeLabel.setText("Automation lane", dontSendNotification);
        automationWriteLabel.setText("Write automation", dontSendNotification);

        volumeSlider.setSliderStyle(Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        volumeSlider.setRange(0.0, 1.0, 0.01);
        volumeSlider.onValueChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                track->mixer.volume = static_cast<float>(volumeSlider.getValue());
                writeAutomationPoint(*track, AutomationLaneMode::volume, track->mixer.volume);
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };
        volumeSlider.onDragStart = [this] { beginAutomationGesture(AutomationLaneMode::volume); };
        volumeSlider.onDragEnd = [this] { endAutomationGesture(AutomationLaneMode::volume); };

        panSlider.setSliderStyle(Slider::LinearHorizontal);
        panSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        panSlider.setRange(-1.0, 1.0, 0.01);
        panSlider.onValueChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                track->mixer.pan = static_cast<float>(panSlider.getValue());
                writeAutomationPoint(*track, AutomationLaneMode::pan, track->mixer.pan);
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };
        panSlider.onDragStart = [this] { beginAutomationGesture(AutomationLaneMode::pan); };
        panSlider.onDragEnd = [this] { endAutomationGesture(AutomationLaneMode::pan); };

        superColliderLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.74f));
        processorLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.60f));
        slotOneLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        slotTwoLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        audioRegionLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.72f));
        audioFileLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.56f));
        regionGainLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.60f));
        volumeLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        panLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        automationModeLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        automationValueLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.56f));
        automationWriteLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        trackNameLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.96f));
        roleLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.64f));

        for (auto* label : { &titleLabel, &trackNameLabel, &roleLabel, &superColliderLabel, &processorLabel, &audioRegionLabel, &audioFileLabel, &regionGainLabel, &volumeLabel, &panLabel, &automationModeLabel, &automationValueLabel, &automationWriteLabel, &slotOneLabel, &slotTwoLabel })
            label->setJustificationType(Justification::topLeft);

        regionGainSlider.setSliderStyle(Slider::LinearHorizontal);
        regionGainSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        regionGainSlider.setRange(0.0, 2.0, 0.01);
        regionGainSlider.setTextValueSuffix("x");
        regionGainSlider.onValueChange = [this]
        {
            if (auto* region = session.getSelectedRegion())
            {
                region->gain = static_cast<float>(regionGainSlider.getValue());
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };

        assignAudioButton.setButtonText("Assign Clip File");
        assignAudioButton.setColour(TextButton::buttonColourId, Colour::fromRGB(76, 86, 106));
        assignAudioButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.92f));
        assignAudioButton.onClick = [this]
        {
            if (onAssignAudioFile != nullptr)
                onAssignAudioFile();
        };

        clearAudioButton.setButtonText("Clear File");
        clearAudioButton.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
        clearAudioButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.88f));
        clearAudioButton.onClick = [this]
        {
            if (onClearAudioFile != nullptr)
                onClearAudioFile();
        };

        auto configureSlotButtons = [] (TextButton& loadButton, TextButton& clearButton)
        {
            loadButton.setButtonText("Load AU");
            loadButton.setColour(TextButton::buttonColourId, Colour::fromRGB(76, 86, 106));
            loadButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.92f));
            clearButton.setButtonText("Clear");
            clearButton.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
            clearButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.88f));
        };

        configureSlotButtons(loadSlotOneButton, clearSlotOneButton);
        configureSlotButtons(loadSlotTwoButton, clearSlotTwoButton);
        auto configureOpenButton = [] (TextButton& button)
        {
            button.setButtonText("Open");
            button.setColour(TextButton::buttonColourId, Colour::fromRGB(67, 73, 88));
            button.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.90f));
        };
        configureOpenButton(openSlotOneButton);
        configureOpenButton(openSlotTwoButton);
        loadSlotOneButton.onClick = [this] { if (onLoadAudioUnit != nullptr) onLoadAudioUnit(0); };
        openSlotOneButton.onClick = [this] { if (onOpenAudioUnit != nullptr) onOpenAudioUnit(0); };
        clearSlotOneButton.onClick = [this] { if (onClearAudioUnit != nullptr) onClearAudioUnit(0); };
        loadSlotTwoButton.onClick = [this] { if (onLoadAudioUnit != nullptr) onLoadAudioUnit(1); };
        openSlotTwoButton.onClick = [this] { if (onOpenAudioUnit != nullptr) onOpenAudioUnit(1); };
        clearSlotTwoButton.onClick = [this] { if (onClearAudioUnit != nullptr) onClearAudioUnit(1); };

        configureAutomationButton(showVolumeAutomationButton, "Volume", AutomationLaneMode::volume);
        configureAutomationButton(showPanAutomationButton, "Pan", AutomationLaneMode::pan);
        hideAutomationButton.setButtonText("Collapse");
        hideAutomationButton.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
        hideAutomationButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.86f));
        hideAutomationButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                track->automationExpanded = false;
                if (onAutomationLayoutChange != nullptr)
                    onAutomationLayoutChange();
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };

        configureAutomationWriteModeButton(readAutomationButton, "Read", AutomationWriteMode::read);
        configureAutomationWriteModeButton(touchAutomationButton, "Touch", AutomationWriteMode::touch);
        configureAutomationWriteModeButton(latchAutomationButton, "Latch", AutomationWriteMode::latch);

        refresh();
    }

    void refresh()
    {
        if (auto* track = session.getSelectedTrack())
        {
            trackNameLabel.setText(track->name, dontSendNotification);
            roleLabel.setText(toDisplayString(track->kind) + " / " + String(track->regions.size()) + " regions", dontSendNotification);
            superColliderLabel.setText(shortenForSidebar(superColliderBridge.describeTrack(*track), 64), dontSendNotification);
            superColliderLabel.setTooltip(superColliderBridge.describeTrack(*track));
            processorLabel.setText("Processor chain: " + shortenForSidebar(describeProcessorChain(*track), 56), dontSendNotification);
            processorLabel.setTooltip(describeProcessorChain(*track));
            slotOneLabel.setText(describePluginSlot(*track, 0), dontSendNotification);
            slotTwoLabel.setText(describePluginSlot(*track, 1), dontSendNotification);
            const auto slotChoicesOne = audioEngine.getAvailablePluginChoices(*track, 0);
            const auto slotChoicesTwo = audioEngine.getAvailablePluginChoices(*track, 1);
            loadSlotOneButton.setEnabled(! slotChoicesOne.empty());
            loadSlotTwoButton.setEnabled(! slotChoicesTwo.empty());
            openSlotOneButton.setEnabled(hasAudioUnitSlot(*track, 0));
            openSlotTwoButton.setEnabled(hasAudioUnitSlot(*track, 1));
            clearSlotOneButton.setEnabled(hasAudioUnitSlot(*track, 0));
            clearSlotTwoButton.setEnabled(hasAudioUnitSlot(*track, 1));
            volumeSlider.setValue(track->mixer.volume, dontSendNotification);
            panSlider.setValue(track->mixer.pan, dontSendNotification);
            updateAutomationUI(*track);

            if (const auto* region = session.getSelectedRegion())
            {
                audioRegionLabel.setText("Selected clip: " + region->name, dontSendNotification);
                audioFileLabel.setText(region->sourceFilePath.isNotEmpty()
                                           ? "File: " + shortenForSidebar(region->sourceFilePath, 60)
                                           : "File: none assigned",
                                       dontSendNotification);
                audioFileLabel.setTooltip(region->sourceFilePath);
                regionGainLabel.setText("Clip gain", dontSendNotification);
                regionGainSlider.setValue(region->gain, dontSendNotification);
                const auto canAssign = region->kind == RegionKind::audio;
                assignAudioButton.setEnabled(canAssign);
                clearAudioButton.setEnabled(canAssign && region->sourceFilePath.isNotEmpty());
                regionGainSlider.setEnabled(canAssign);
            }
            else
            {
                audioRegionLabel.setText("Selected clip: none", dontSendNotification);
                audioFileLabel.setText("Click an audio clip in the arrange area to assign a file.", dontSendNotification);
                audioFileLabel.setTooltip({});
                regionGainLabel.setText("Clip gain", dontSendNotification);
                regionGainSlider.setValue(1.0, dontSendNotification);
                assignAudioButton.setEnabled(false);
                clearAudioButton.setEnabled(false);
                regionGainSlider.setEnabled(false);
            }
        }
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour::fromRGB(23, 26, 33));
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));

        paintSection(g, clipSectionBounds, "Selected Clip");
        paintSection(g, trackSectionBounds, "Track");
        paintSection(g, channelSectionBounds, "Channel");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        titleLabel.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);

        const auto remainingHeight = area.getHeight();
        const auto clipHeight = jmin(176, remainingHeight);
        clipSectionBounds = area.removeFromTop(clipHeight);
        area.removeFromTop(jmin(12, area.getHeight()));

        const auto trackHeight = area.getHeight() >= 156 ? jmin(156, area.getHeight()) : 0;
        trackSectionBounds = trackHeight > 0 ? area.removeFromTop(trackHeight) : Rectangle<int>();
        if (trackHeight > 0)
            area.removeFromTop(jmin(12, area.getHeight()));

        const auto channelHeight = area.getHeight() >= 94 ? area.getHeight() : 0;
        channelSectionBounds = channelHeight > 0 ? area.removeFromTop(channelHeight) : Rectangle<int>();

        auto clipArea = clipSectionBounds.reduced(14, 12);
        clipArea.removeFromTop(10);
        audioRegionLabel.setBounds(clipArea.removeFromTop(20));
        clipArea.removeFromTop(2);
        audioFileLabel.setBounds(clipArea.removeFromTop(26));
        clipArea.removeFromTop(5);
        auto clipButtons = clipArea.removeFromTop(26);
        assignAudioButton.setBounds(clipButtons.removeFromLeft(158).reduced(0, 1));
        clipButtons.removeFromLeft(8);
        clearAudioButton.setBounds(clipButtons.removeFromLeft(102).reduced(0, 1));
        clipArea.removeFromTop(8);
        regionGainLabel.setBounds(clipArea.removeFromTop(16));
        clipArea.removeFromTop(4);
        regionGainSlider.setBounds(clipArea.removeFromTop(32));

        if (! trackSectionBounds.isEmpty())
        {
            auto trackArea = trackSectionBounds.reduced(14, 16);
            trackArea.removeFromTop(12);
            trackNameLabel.setBounds(trackArea.removeFromTop(28));
            roleLabel.setBounds(trackArea.removeFromTop(20));
            trackArea.removeFromTop(4);
            superColliderLabel.setBounds(trackArea.removeFromTop(22));
            trackArea.removeFromTop(6);
            automationModeLabel.setBounds(trackArea.removeFromTop(16));
            auto laneButtons = trackArea.removeFromTop(28);
            showVolumeAutomationButton.setBounds(laneButtons.removeFromLeft(90).reduced(0, 1));
            laneButtons.removeFromLeft(6);
            showPanAutomationButton.setBounds(laneButtons.removeFromLeft(90).reduced(0, 1));
            laneButtons.removeFromLeft(6);
            hideAutomationButton.setBounds(laneButtons.removeFromLeft(68).reduced(0, 1));
            trackArea.removeFromTop(4);
            automationValueLabel.setBounds(trackArea.removeFromTop(16));
            trackArea.removeFromTop(4);
            automationWriteLabel.setBounds(trackArea.removeFromTop(16));
            auto writeButtons = trackArea.removeFromTop(26);
            readAutomationButton.setBounds(writeButtons.removeFromLeft(74).reduced(0, 1));
            writeButtons.removeFromLeft(6);
            touchAutomationButton.setBounds(writeButtons.removeFromLeft(74).reduced(0, 1));
            writeButtons.removeFromLeft(6);
            latchAutomationButton.setBounds(writeButtons.removeFromLeft(74).reduced(0, 1));
        }
        else
        {
            trackNameLabel.setBounds({});
            roleLabel.setBounds({});
            superColliderLabel.setBounds({});
            automationModeLabel.setBounds({});
            automationValueLabel.setBounds({});
            automationWriteLabel.setBounds({});
            readAutomationButton.setBounds({});
            touchAutomationButton.setBounds({});
            latchAutomationButton.setBounds({});
            showVolumeAutomationButton.setBounds({});
            showPanAutomationButton.setBounds({});
            hideAutomationButton.setBounds({});
        }

        if (! channelSectionBounds.isEmpty())
        {
            auto channelArea = channelSectionBounds.reduced(14, 16);
            channelArea.removeFromTop(12);
            processorLabel.setBounds(channelArea.removeFromTop(18));
            channelArea.removeFromTop(6);
            slotOneLabel.setBounds(channelArea.removeFromTop(18));
            auto slotOneButtons = channelArea.removeFromTop(24);
            loadSlotOneButton.setBounds(slotOneButtons.removeFromLeft(82).reduced(0, 1));
            slotOneButtons.removeFromLeft(6);
            openSlotOneButton.setBounds(slotOneButtons.removeFromLeft(58).reduced(0, 1));
            slotOneButtons.removeFromLeft(6);
            clearSlotOneButton.setBounds(slotOneButtons.removeFromLeft(62).reduced(0, 1));
            channelArea.removeFromTop(6);
            slotTwoLabel.setBounds(channelArea.removeFromTop(18));
            auto slotTwoButtons = channelArea.removeFromTop(24);
            loadSlotTwoButton.setBounds(slotTwoButtons.removeFromLeft(82).reduced(0, 1));
            slotTwoButtons.removeFromLeft(6);
            openSlotTwoButton.setBounds(slotTwoButtons.removeFromLeft(58).reduced(0, 1));
            slotTwoButtons.removeFromLeft(6);
            clearSlotTwoButton.setBounds(slotTwoButtons.removeFromLeft(62).reduced(0, 1));
            channelArea.removeFromTop(8);
            volumeLabel.setBounds(channelArea.removeFromTop(16));
            volumeSlider.setBounds(channelArea.removeFromTop(24));
            channelArea.removeFromTop(4);
            panLabel.setBounds(channelArea.removeFromTop(16));
            panSlider.setBounds(channelArea.removeFromTop(24));
        }
        else
        {
            processorLabel.setBounds({});
            slotOneLabel.setBounds({});
            slotTwoLabel.setBounds({});
            loadSlotOneButton.setBounds({});
            openSlotOneButton.setBounds({});
            clearSlotOneButton.setBounds({});
            loadSlotTwoButton.setBounds({});
            openSlotTwoButton.setBounds({});
            clearSlotTwoButton.setBounds({});
            volumeLabel.setBounds({});
            panLabel.setBounds({});
            volumeSlider.setBounds({});
            panSlider.setBounds({});
        }
    }

private:
    juce::String describePluginSlot(const TrackState& track, int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= static_cast<int>(track.inserts.size()))
            return slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi)
                ? "Instrument slot: empty"
                : "Insert " + juce::String(slotIndex + 1) + ": empty";

        const auto& slot = track.inserts[static_cast<size_t>(slotIndex)];
        if (slot.kind != ProcessorKind::audioUnit || slot.pluginIdentifier.isEmpty())
            return slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi)
                ? "Instrument slot: empty"
                : "Insert " + juce::String(slotIndex + 1) + ": empty";

        const auto prefix = slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi)
            ? "Instrument"
            : "Insert " + juce::String(slotIndex + 1);
        return prefix + ": " + shortenForSidebar(slot.name, 28);
    }

    bool hasAudioUnitSlot(const TrackState& track, int slotIndex) const
    {
        return slotIndex >= 0
            && slotIndex < static_cast<int>(track.inserts.size())
            && track.inserts[static_cast<size_t>(slotIndex)].kind == ProcessorKind::audioUnit
            && track.inserts[static_cast<size_t>(slotIndex)].pluginIdentifier.isNotEmpty();
    }

    void configureAutomationButton(TextButton& button, const String& text, AutomationLaneMode mode)
    {
        button.setButtonText(text);
        button.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
        button.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.86f));
        button.onClick = [this, mode]
        {
            if (auto* track = session.getSelectedTrack())
            {
                track->visibleAutomationLane = mode;
                track->automationExpanded = true;
                if (onAutomationLayoutChange != nullptr)
                    onAutomationLayoutChange();
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };
    }

    void configureAutomationWriteModeButton(TextButton& button, const String& text, AutomationWriteMode mode)
    {
        button.setButtonText(text);
        button.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
        button.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.86f));
        button.onClick = [this, mode]
        {
            if (auto* track = session.getSelectedTrack())
            {
                track->automationWriteMode = mode;
                if (mode == AutomationWriteMode::read)
                {
                    track->automationGestureActive = false;
                    track->automationLatchActive = false;
                    track->automationWriteTarget = AutomationLaneMode::none;
                }
                updateAutomationUI(*track);
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            }
        };
    }

    void updateAutomationUI(const TrackState& track)
    {
        showVolumeAutomationButton.setToggleState(track.visibleAutomationLane == AutomationLaneMode::volume && track.automationExpanded, dontSendNotification);
        showPanAutomationButton.setToggleState(track.visibleAutomationLane == AutomationLaneMode::pan && track.automationExpanded, dontSendNotification);
        hideAutomationButton.setToggleState(! track.automationExpanded, dontSendNotification);
        readAutomationButton.setToggleState(track.automationWriteMode == AutomationWriteMode::read, dontSendNotification);
        touchAutomationButton.setToggleState(track.automationWriteMode == AutomationWriteMode::touch, dontSendNotification);
        latchAutomationButton.setToggleState(track.automationWriteMode == AutomationWriteMode::latch, dontSendNotification);

        auto tintModeButton = [] (TextButton& button, bool active)
        {
            button.setColour(TextButton::buttonColourId,
                             active ? Colour::fromRGB(196, 88, 72) : Colour::fromRGB(54, 60, 74));
        };

        tintModeButton(readAutomationButton, track.automationWriteMode == AutomationWriteMode::read);
        tintModeButton(touchAutomationButton, track.automationWriteMode == AutomationWriteMode::touch);
        tintModeButton(latchAutomationButton, track.automationWriteMode == AutomationWriteMode::latch);

        if (! track.automationExpanded)
        {
            automationValueLabel.setText("Automation collapsed / mode " + toDisplayString(track.automationWriteMode), dontSendNotification);
            return;
        }

        const auto& points = track.visibleAutomationLane == AutomationLaneMode::pan ? track.panAutomation : track.volumeAutomation;
        const auto fallback = track.visibleAutomationLane == AutomationLaneMode::pan ? track.mixer.pan : track.mixer.volume;
        const auto value = interpolateAutomationDisplayValue(points, session.transport.playheadBeat, fallback);
        const auto suffix = track.automationWriteMode == AutomationWriteMode::read
            ? " / read"
            : (track.automationGestureActive || track.automationLatchActive ? " / writing" : " / armed");

        if (track.visibleAutomationLane == AutomationLaneMode::pan)
            automationValueLabel.setText("Current pan: " + String(value, 2) + suffix, dontSendNotification);
        else
            automationValueLabel.setText("Current volume: " + String(value, 2) + suffix, dontSendNotification);
    }

    void beginAutomationGesture(AutomationLaneMode laneMode)
    {
        if (auto* track = session.getSelectedTrack())
        {
            track->visibleAutomationLane = laneMode;
            track->automationExpanded = true;
            track->automationWriteTarget = laneMode;
            track->automationGestureActive = true;
            if (track->automationWriteMode == AutomationWriteMode::latch)
                track->automationLatchActive = true;

            if (onAutomationLayoutChange != nullptr)
                onAutomationLayoutChange();
            if (onAutomationEdited != nullptr)
                onAutomationEdited();
        }
    }

    void endAutomationGesture(AutomationLaneMode)
    {
        if (auto* track = session.getSelectedTrack())
        {
            track->automationGestureActive = false;
            if (track->automationWriteMode == AutomationWriteMode::touch)
            {
                track->automationWriteTarget = AutomationLaneMode::none;
                track->automationLatchActive = false;
            }
            updateAutomationUI(*track);
            if (onAutomationEdited != nullptr)
                onAutomationEdited();
        }
    }

    void writeAutomationPoint(TrackState& track, AutomationLaneMode laneMode, float value)
    {
        if (! session.transport.playing || track.automationWriteMode == AutomationWriteMode::read)
            return;

        const auto shouldWrite = track.automationWriteMode == AutomationWriteMode::touch
            ? track.automationGestureActive && track.automationWriteTarget == laneMode
            : ((track.automationGestureActive || track.automationLatchActive) && track.automationWriteTarget == laneMode);

        if (! shouldWrite)
            return;

        track.visibleAutomationLane = laneMode;
        track.automationExpanded = true;

        auto& points = laneMode == AutomationLaneMode::pan ? track.panAutomation : track.volumeAutomation;
        const auto writeBeat = session.transport.playheadBeat;
        const auto clampedValue = laneMode == AutomationLaneMode::pan ? juce::jlimit(-1.0f, 1.0f, value) : juce::jlimit(0.0f, 1.0f, value);

        auto nearby = std::find_if(points.begin(), points.end(), [writeBeat] (const auto& point)
        {
            return std::abs(point.beat - writeBeat) <= 0.15;
        });

        if (nearby != points.end())
        {
            nearby->beat = writeBeat;
            nearby->value = clampedValue;
        }
        else
        {
            AutomationPoint point;
            point.beat = writeBeat;
            point.value = clampedValue;
            points.push_back(point);
            std::sort(points.begin(), points.end(), [] (const auto& left, const auto& right) { return left.beat < right.beat; });
        }

        if (onAutomationEdited != nullptr)
            onAutomationEdited();
    }

    void paintSection(Graphics& g, Rectangle<int> bounds, const String& title)
    {
        if (bounds.isEmpty())
            return;

        g.setColour(Colour::fromRGB(28, 33, 42));
        g.fillRoundedRectangle(bounds.toFloat(), 12.0f);
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 12.0f, 1.0f);
        g.setColour(Colours::white.withAlpha(0.52f));
        g.setFont(FontOptions(11.0f, Font::bold));
        g.drawText(title.toUpperCase(), bounds.reduced(14, 10).removeFromTop(14), Justification::centredLeft, false);
    }

    SessionState& session;
    const SuperColliderBridge& superColliderBridge;
    AudioEngine& audioEngine;
    std::function<void()> onAssignAudioFile;
    std::function<void()> onClearAudioFile;
    std::function<void(int)> onLoadAudioUnit;
    std::function<void(int)> onOpenAudioUnit;
    std::function<void(int)> onClearAudioUnit;
    std::function<void()> onAutomationLayoutChange;
    std::function<void()> onAutomationEdited;
    Label titleLabel;
    Label trackNameLabel;
    Label roleLabel;
    Label superColliderLabel;
    Label processorLabel;
    Label audioRegionLabel;
    Label audioFileLabel;
    Label regionGainLabel;
    Label volumeLabel;
    Label panLabel;
    Label automationModeLabel;
    Label automationValueLabel;
    Label automationWriteLabel;
    Slider regionGainSlider;
    TextButton assignAudioButton;
    TextButton clearAudioButton;
    TextButton showVolumeAutomationButton;
    TextButton showPanAutomationButton;
    TextButton hideAutomationButton;
    TextButton readAutomationButton;
    TextButton touchAutomationButton;
    TextButton latchAutomationButton;
    Label slotOneLabel;
    Label slotTwoLabel;
    TextButton loadSlotOneButton;
    TextButton openSlotOneButton;
    TextButton clearSlotOneButton;
    TextButton loadSlotTwoButton;
    TextButton openSlotTwoButton;
    TextButton clearSlotTwoButton;
    Slider volumeSlider;
    Slider panSlider;
    Rectangle<int> trackSectionBounds;
    Rectangle<int> clipSectionBounds;
    Rectangle<int> channelSectionBounds;
};

class MixerStripComponent final : public Component
{
public:
    MixerStripComponent(TrackState& trackToUse, std::function<void()> onEditToUse)
        : track(trackToUse), onEdit(std::move(onEditToUse))
    {
        nameLabel.setText(track.name, dontSendNotification);
        nameLabel.setJustificationType(Justification::centred);

        addAndMakeVisible(nameLabel);
        addAndMakeVisible(kindLabel);
        addAndMakeVisible(volumeSlider);
        addAndMakeVisible(panSlider);
        addAndMakeVisible(meter);

        kindLabel.setText(toDisplayString(track.kind), dontSendNotification);
        kindLabel.setJustificationType(Justification::centred);
        kindLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.55f));

        volumeSlider.setSliderStyle(Slider::LinearVertical);
        volumeSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        volumeSlider.setRange(0.0, 1.0, 0.01);
        volumeSlider.setValue(track.mixer.volume);
        volumeSlider.onValueChange = [this]
        {
            track.mixer.volume = static_cast<float>(volumeSlider.getValue());
            if (onEdit != nullptr)
                onEdit();
        };

        panSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        panSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        panSlider.setRange(-1.0, 1.0, 0.01);
        panSlider.setValue(track.mixer.pan);
        panSlider.onValueChange = [this]
        {
            track.mixer.pan = static_cast<float>(panSlider.getValue());
            if (onEdit != nullptr)
                onEdit();
        };
    }

    void setMeterLevel(float level)
    {
        meter.setLevel(level);
    }

    void paint(Graphics& g) override
    {
        g.fillAll(track.selected ? Colour::fromRGB(44, 50, 63) : Colour::fromRGB(29, 33, 41));
        g.setColour(track.colour);
        g.fillRect(12, 10, getWidth() - 24, 6);
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(12);
        nameLabel.setBounds(area.removeFromTop(24));
        kindLabel.setBounds(area.removeFromTop(18));
        panSlider.setBounds(area.removeFromTop(54));
        area.removeFromTop(8);
        auto faderArea = area.removeFromTop(160);
        volumeSlider.setBounds(faderArea.removeFromLeft(46));
        meter.setBounds(faderArea.removeFromLeft(18));
    }

private:
    TrackState& track;
    std::function<void()> onEdit;
    Label nameLabel;
    Label kindLabel;
    Slider volumeSlider;
    Slider panSlider;
    MeterComponent meter;
};

class MainComponent::MixerComponent final : public Component
{
public:
    MixerComponent(SessionState& sessionToUse, std::function<void()> onEditToUse)
        : session(sessionToUse), onEdit(std::move(onEditToUse))
    {
        rebuildStrips();
    }

    void refresh()
    {
        for (size_t i = 0; i < strips.size(); ++i)
            strips[i]->setMeterLevel(session.tracks[i].mixer.meterLevel);

        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        constexpr int stripWidth = 118;

        for (auto& strip : strips)
            strip->setBounds(area.removeFromLeft(stripWidth).reduced(5, 0));
    }

private:
    void rebuildStrips()
    {
        strips.clear();
        removeAllChildren();

        for (auto& track : session.tracks)
        {
            auto strip = std::make_unique<MixerStripComponent>(track, onEdit);
            addAndMakeVisible(*strip);
            strips.push_back(std::move(strip));
        }
    }

    SessionState& session;
    std::function<void()> onEdit;
    std::vector<std::unique_ptr<MixerStripComponent>> strips;
};

class MainComponent::SuperColliderOverviewComponent final : public Component
{
public:
    SuperColliderOverviewComponent(SessionState& sessionToUse,
                                   const SuperColliderBridge& bridgeToUse,
                                   std::function<void()> onRebuildToUse)
        : session(sessionToUse), superColliderBridge(bridgeToUse), onRebuild(std::move(onRebuildToUse))
    {
        addAndMakeVisible(rebuildButton);
        rebuildButton.setButtonText("Rebuild SynthDefs");
        rebuildButton.onClick = [this]
        {
            if (onRebuild != nullptr)
                onRebuild();
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        auto topRow = area.removeFromTop(32);
        rebuildButton.setBounds(topRow.removeFromRight(136));
    }

    void setRebuildInProgress(bool shouldShowBusyState)
    {
        rebuildInProgress = shouldShowBusyState;
        rebuildButton.setEnabled(! rebuildInProgress);
        rebuildButton.setButtonText(rebuildInProgress ? "Rebuilding..." : "Rebuild SynthDefs");
        repaint();
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour::fromRGB(25, 29, 37));
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);

        auto area = getLocalBounds().reduced(14);
        g.setColour(Colours::white.withAlpha(0.92f));
        g.setFont(FontOptions(16.0f, Font::bold));
        auto titleRow = area.removeFromTop(32);
        g.drawFittedText("SuperCollider Routing", titleRow.removeFromLeft(getWidth() - 170), Justification::centredLeft, 1);
        area.removeFromTop(6);

        const auto& runtime = superColliderBridge.getRuntimeState();
        auto paintSectionTitle = [&g, &area] (const String& text)
        {
            g.setColour(Colours::white.withAlpha(0.52f));
            g.setFont(FontOptions(11.0f, Font::bold));
            g.drawText(text.toUpperCase(), area.removeFromTop(14), Justification::centredLeft, false);
            area.removeFromTop(4);
            g.setFont(FontOptions(13.0f));
        };

        paintSectionTitle("Server");
        g.setColour(Colours::white.withAlpha(0.66f));
        g.drawFittedText(shortenForSidebar(superColliderBridge.getConnectionSummary(session), 70),
                         area.removeFromTop(20), Justification::centredLeft, 1);
        g.setColour(Colours::white.withAlpha(0.48f));
        g.drawText(runtime.lastOscAction.isNotEmpty() ? shortenForSidebar(runtime.lastOscAction, 72) : "OSC session idle",
                   area.removeFromTop(18), Justification::centredLeft, false);
        g.drawText(runtime.diagnostics.isNotEmpty() ? shortenForSidebar(runtime.diagnostics, 74) : "No runtime diagnostics",
                   area.removeFromTop(30), Justification::topLeft, true);

        area.removeFromTop(8);
        paintSectionTitle("SynthDefs");
        g.setColour(Colours::white.withAlpha(0.56f));
        g.drawText("Sources " + String(runtime.sourceSynthDefCount)
                       + " | Loaded " + String(runtime.loadedSynthDefCount)
                       + " | Auto-built " + String(runtime.autoCompiledSynthDefCount),
                   area.removeFromTop(20), Justification::centredLeft, false);
        g.drawText(runtime.synthDefDirectoryPath.isNotEmpty()
                       ? "Folder: " + shortenForSidebar(runtime.synthDefDirectoryPath, 72)
                       : "Folder: not found",
                   area.removeFromTop(32), Justification::topLeft, true);

        area.removeFromTop(8);
        paintSectionTitle("Active Routes");
        auto snapshots = superColliderBridge.createSnapshots(session);
        auto visibleSnapshots = 0;

        for (const auto& snapshot : snapshots)
        {
            if (! (snapshot.hasRenderScript || snapshot.hasFxInsert || snapshot.hasMidiGenerator))
                continue;

            if (visibleSnapshots == 2)
                break;

            auto row = area.removeFromTop(22);

            g.setColour(Colours::white.withAlpha(0.88f));
            g.drawText(shortenForSidebar(snapshot.trackName + " / " + toDisplayString(snapshot.trackKind), 28),
                       row.removeFromLeft(148), Justification::centredLeft, false);
            g.setColour(Colours::white.withAlpha(0.55f));
            g.drawText(shortenForSidebar(snapshot.routingSummary, 36), row, Justification::centredLeft, false);
            area.removeFromTop(4);
            ++visibleSnapshots;
        }

        if (rebuildInProgress)
        {
            g.setColour(Colours::black.withAlpha(0.18f));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f);
        }
    }

private:
    SessionState& session;
    const SuperColliderBridge& superColliderBridge;
    std::function<void()> onRebuild;
    TextButton rebuildButton;
    bool rebuildInProgress { false };
};

class MainComponent::PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::String title,
                       std::unique_ptr<juce::AudioProcessorEditor> editorToUse,
                       int trackIdToUse,
                       int slotIndexToUse,
                       std::function<void(int, int)> onCloseToUse)
        : juce::DocumentWindow(title,
                               juce::Colour::fromRGB(27, 31, 39),
                               juce::DocumentWindow::closeButton),
          editor(std::move(editorToUse)),
          trackId(trackIdToUse),
          slotIndex(slotIndexToUse),
          onClose(std::move(onCloseToUse))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);

        if (editor != nullptr)
        {
            setContentOwned(editor.release(), true);
            centreWithSize(juce::jmax(420, getWidth()), juce::jmax(260, getHeight()));
        }
    }

    int getTrackId() const { return trackId; }
    int getSlotIndex() const { return slotIndex; }

    void closeButtonPressed() override
    {
        if (onClose != nullptr)
            onClose(trackId, slotIndex);
    }

private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int trackId { 0 };
    int slotIndex { 0 };
    std::function<void(int, int)> onClose;
};

MainComponent::MainComponent()
    : audioEngine(session, superColliderBridge)
{
    superColliderBridge.refreshEnvironment(session);
    superColliderBridge.ensureServerRunning(session);

    deviceManager.initialiseWithDefaultDevices(0, 2);
    deviceManager.addAudioCallback(&audioEngine);

    transport = std::make_unique<TransportComponent>(session.transport,
                                                     audioEngine,
                                                     [this] { performUndo(); },
                                                     [this] { performRedo(); },
                                                     [this] { saveProject(false); },
                                                     [this] { loadProject(); });
    arrangeView = std::make_unique<ArrangeViewComponent>(session,
                                                         [this] (int trackId) { selectTrack(trackId); },
                                                         [this] (int trackId, int regionIndex) { selectRegion(trackId, regionIndex); },
                                                         [this] { regionEdited(); },
                                                         [this] {
                                                             refreshAllViews(true);
                                                         },
                                                         [this] { markSessionChanged(true); });
    inspector = std::make_unique<InspectorComponent>(session,
                                                     superColliderBridge,
                                                     audioEngine,
                                                     [this] { assignAudioFileToSelectedRegion(); },
                                                     [this] { clearAudioFileFromSelectedRegion(); },
                                                     [this] (int slotIndex) { loadAudioUnitIntoSelectedTrack(slotIndex); },
                                                     [this] (int slotIndex) { openAudioUnitEditorForSelectedTrack(slotIndex); },
                                                     [this] (int slotIndex) { clearAudioUnitFromSelectedTrack(slotIndex); },
                                                     [this] {
                                                         markSessionChanged(true);
                                                     },
                                                     [this] {
                                                         markSessionChanged(false);
                                                     });
    mixer = std::make_unique<MixerComponent>(session, [this] { markSessionChanged(false); });
    pianoRoll = std::make_unique<PianoRollComponent>(session, [this] { regionEdited(); });
    superColliderOverview = std::make_unique<SuperColliderOverviewComponent>(session, superColliderBridge, [this] { rebuildSynthDefs(); });

    addAndMakeVisible(*transport);
    addAndMakeVisible(*arrangeView);
    addAndMakeVisible(*inspector);
    addAndMakeVisible(*pianoRoll);
    addAndMakeVisible(*mixer);
    addAndMakeVisible(*superColliderOverview);

    setWantsKeyboardFocus(true);
    undoSnapshots.push_back(serialiseSessionToJson(session));
    updateWindowState();
    startTimerHz(24);
    setSize(1580, 960);
}

MainComponent::~MainComponent()
{
    closeAllAudioUnitEditorWindows();
    deviceManager.removeAudioCallback(&audioEngine);
    superColliderBridge.shutdown(session);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    transport->setBounds(area.removeFromTop(76));

    const auto* selectedRegion = session.getSelectedRegion();
    const auto showPianoRoll = selectedRegion != nullptr
        && (selectedRegion->kind == RegionKind::midi || selectedRegion->kind == RegionKind::generated);

    auto bottom = area.removeFromBottom(showPianoRoll ? 350 : 250);
    auto rightSidebar = area.removeFromRight(360);

    superColliderOverview->setBounds(rightSidebar.removeFromTop(216).reduced(10, 8));
    inspector->setBounds(rightSidebar.reduced(8));
    arrangeView->setBounds(area);

    if (showPianoRoll)
    {
        pianoRoll->setVisible(true);
        auto pianoArea = bottom.removeFromTop(194);
        pianoRoll->setBounds(pianoArea.reduced(10, 6));
        mixer->setBounds(bottom);
    }
    else
    {
        pianoRoll->setVisible(false);
        pianoRoll->setBounds({});
        mixer->setBounds(bottom);
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        performUndo();
        return true;
    }

    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
    {
        performRedo();
        return true;
    }

    if (key == juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0))
    {
        saveProject(false);
        return true;
    }

    if (key == juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0))
    {
        loadProject();
        return true;
    }

    return Component::keyPressed(key);
}

void MainComponent::timerCallback()
{
    if (undoSnapshotPending && juce::Time::getMillisecondCounter() - lastMutationTimeMs > 250)
        commitUndoSnapshotNow();

    superColliderBridge.poll(session);

    for (auto& track : session.tracks)
    {
        if (! session.transport.playing)
        {
            track.automationGestureActive = false;
            track.automationLatchActive = false;
            track.automationWriteTarget = AutomationLaneMode::none;
            continue;
        }

        const auto shouldWrite = track.automationWriteMode == AutomationWriteMode::touch
            ? track.automationGestureActive
            : (track.automationWriteMode == AutomationWriteMode::latch && (track.automationGestureActive || track.automationLatchActive));

        if (! shouldWrite)
            continue;

        if (track.automationWriteTarget == AutomationLaneMode::volume)
        {
            AutomationPoint point;
            point.beat = session.transport.playheadBeat;
            point.value = juce::jlimit(0.0f, 1.0f, track.mixer.volume);

            auto nearby = std::find_if(track.volumeAutomation.begin(), track.volumeAutomation.end(), [beat = point.beat] (const auto& existing)
            {
                return std::abs(existing.beat - beat) <= 0.15;
            });

            if (nearby != track.volumeAutomation.end())
            {
                nearby->beat = point.beat;
                nearby->value = point.value;
            }
            else
            {
                track.volumeAutomation.push_back(point);
                std::sort(track.volumeAutomation.begin(), track.volumeAutomation.end(), [] (const auto& left, const auto& right) {
                    return left.beat < right.beat;
                });
            }
        }
        else if (track.automationWriteTarget == AutomationLaneMode::pan)
        {
            AutomationPoint point;
            point.beat = session.transport.playheadBeat;
            point.value = juce::jlimit(-1.0f, 1.0f, track.mixer.pan);

            auto nearby = std::find_if(track.panAutomation.begin(), track.panAutomation.end(), [beat = point.beat] (const auto& existing)
            {
                return std::abs(existing.beat - beat) <= 0.15;
            });

            if (nearby != track.panAutomation.end())
            {
                nearby->beat = point.beat;
                nearby->value = point.value;
            }
            else
            {
                track.panAutomation.push_back(point);
                std::sort(track.panAutomation.begin(), track.panAutomation.end(), [] (const auto& left, const auto& right) {
                    return left.beat < right.beat;
                });
            }
        }
    }

    refreshAllViews(false);
}

void MainComponent::selectTrack(int trackId)
{
    session.selectTrack(trackId);
    refreshAllViews(true);
    resized();
}

void MainComponent::selectRegion(int trackId, int regionIndex)
{
    session.selectRegion(trackId, regionIndex);
    refreshAllViews(true);
    resized();
}

void MainComponent::regionEdited()
{
    markSessionChanged(false);
}

void MainComponent::refreshAllViews(bool refreshLayout)
{
    if (transport != nullptr)
        updateWindowState();

    if (inspector != nullptr)
        inspector->refresh();

    if (arrangeView != nullptr)
    {
        if (refreshLayout)
            arrangeView->refreshTracks();
        else
            arrangeView->repaint();
    }

    if (pianoRoll != nullptr)
        pianoRoll->refresh();

    if (mixer != nullptr)
        mixer->refresh();

    if (superColliderOverview != nullptr)
        superColliderOverview->repaint();
}

void MainComponent::markSessionChanged(bool needsLayoutRefresh, bool commitImmediately)
{
    sessionDirty = true;
    redoSnapshots.clear();
    if (needsLayoutRefresh)
        refreshAllViews(true);
    else
        refreshAllViews(false);

    lastMutationTimeMs = juce::Time::getMillisecondCounter();
    undoSnapshotPending = ! commitImmediately;
    if (commitImmediately)
        commitUndoSnapshotNow();
    else
        updateWindowState();
}

void MainComponent::commitUndoSnapshotNow()
{
    if (suppressUndoCapture)
        return;

    undoSnapshotPending = false;
    const auto snapshot = serialiseSessionToJson(session);
    if (! undoSnapshots.empty() && undoSnapshots.back() == snapshot)
    {
        updateWindowState();
        return;
    }

    undoSnapshots.push_back(snapshot);
    constexpr size_t maximumSnapshots = 200;
    if (undoSnapshots.size() > maximumSnapshots)
        undoSnapshots.erase(undoSnapshots.begin(), undoSnapshots.begin() + static_cast<std::ptrdiff_t>(undoSnapshots.size() - maximumSnapshots));

    updateWindowState();
}

void MainComponent::applySessionSnapshot(const juce::String& snapshotJson)
{
    suppressUndoCapture = true;
    closeAllAudioUnitEditorWindows();
    session.transport.playing = false;
    session.transport.recording = false;
    if (deserialiseSessionFromJson(session, snapshotJson).wasOk())
    {
        audioEngine.reloadSessionState();
        superColliderBridge.refreshEnvironment(session);
        superColliderBridge.ensureServerRunning(session);
    }
    suppressUndoCapture = false;
    refreshAllViews(true);
    resized();
    updateWindowState();
}

void MainComponent::performUndo()
{
    if (undoSnapshots.size() <= 1)
        return;

    if (undoSnapshotPending)
        commitUndoSnapshotNow();

    if (undoSnapshots.size() <= 1)
        return;

    redoSnapshots.push_back(undoSnapshots.back());
    undoSnapshots.pop_back();
    applySessionSnapshot(undoSnapshots.back());
    sessionDirty = true;
    updateWindowState();
}

void MainComponent::performRedo()
{
    if (redoSnapshots.empty())
        return;

    const auto snapshot = redoSnapshots.back();
    redoSnapshots.pop_back();
    undoSnapshots.push_back(snapshot);
    applySessionSnapshot(snapshot);
    sessionDirty = true;
    updateWindowState();
}

void MainComponent::saveProject(bool saveAs)
{
    commitUndoSnapshotNow();

    if (! saveAs && currentProjectPath.isNotEmpty())
    {
        const auto result = saveSessionToFile(session, juce::File(currentProjectPath));
        if (result.wasOk())
        {
            sessionDirty = false;
            updateWindowState();
        }
        return;
    }

    activeProjectChooser = std::make_unique<juce::FileChooser>("Save project",
                                                               juce::File(currentProjectPath.isNotEmpty() ? currentProjectPath : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LogicLikeDAW.logicdaw")),
                                                               "*.logicdaw");
    activeProjectChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto selected = chooser.getResult();
                                          activeProjectChooser.reset();
                                          if (selected == juce::File())
                                              return;

                                          auto target = selected;
                                          if (! target.hasFileExtension(".logicdaw"))
                                              target = target.withFileExtension(".logicdaw");

                                          const auto result = saveSessionToFile(session, target);
                                          if (result.wasOk())
                                          {
                                              currentProjectPath = target.getFullPathName();
                                              sessionDirty = false;
                                              updateWindowState();
                                          }
                                      });
}

void MainComponent::loadProject()
{
    activeProjectChooser = std::make_unique<juce::FileChooser>("Load project",
                                                               juce::File(currentProjectPath),
                                                               "*.logicdaw");
    activeProjectChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto selected = chooser.getResult();
                                          activeProjectChooser.reset();
                                          if (! selected.existsAsFile())
                                              return;

                                          SessionState loadedSession;
                                          const auto result = loadSessionFromFile(loadedSession, selected);
                                          if (result.failed())
                                              return;

                                          closeAllAudioUnitEditorWindows();
                                          session = std::move(loadedSession);
                                          session.transport.playing = false;
                                          session.transport.recording = false;
                                          currentProjectPath = selected.getFullPathName();
                                          sessionDirty = false;
                                          undoSnapshots.clear();
                                          redoSnapshots.clear();
                                          undoSnapshots.push_back(serialiseSessionToJson(session));
                                          audioEngine.reloadSessionState();
                                          superColliderBridge.refreshEnvironment(session);
                                          superColliderBridge.ensureServerRunning(session);
                                          refreshAllViews(true);
                                          resized();
                                          updateWindowState();
                                      });
}

void MainComponent::updateWindowState()
{
    if (transport == nullptr)
        return;

    auto projectName = currentProjectPath.isNotEmpty()
        ? juce::File(currentProjectPath).getFileNameWithoutExtension()
        : juce::String("Untitled Project");
    transport->setProjectStatus(projectName, sessionDirty || undoSnapshotPending, undoSnapshots.size() > 1, ! redoSnapshots.empty());
}

void MainComponent::rebuildSynthDefs()
{
    if (synthDefRebuildInProgress)
        return;

    synthDefRebuildInProgress = true;
    superColliderOverview->setRebuildInProgress(true);
    superColliderOverview->repaint();

    superColliderBridge.rebuildSynthDefs(session);
    refreshAllViews(true);
    superColliderOverview->setRebuildInProgress(false);
    superColliderOverview->repaint();
    synthDefRebuildInProgress = false;
}

void MainComponent::assignAudioFileToSelectedRegion()
{
    auto* track = session.getSelectedTrack();
    if (track == nullptr)
        return;

    auto* region = session.getSelectedRegion();
    if (region == nullptr || region->kind != RegionKind::audio)
        return;

    const auto selectedTrackId = track->id;
    const auto selectedRegionIndex = session.selectedRegionIndex;
    activeFileChooser = std::make_unique<juce::FileChooser>("Choose an audio file for " + region->name,
                                                            juce::File(),
                                                            "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this, selectedTrackId, selectedRegionIndex] (const juce::FileChooser& chooser)
                                   {
                                       const auto result = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (! result.existsAsFile())
                                           return;

                                       auto it = std::find_if(session.tracks.begin(), session.tracks.end(), [selectedTrackId] (const auto& candidate) {
                                           return candidate.id == selectedTrackId;
                                       });

                                       if (it == session.tracks.end())
                                           return;

                                       if (selectedRegionIndex >= 0
                                           && selectedRegionIndex < static_cast<int>(it->regions.size()))
                                       {
                                           auto* selectedRegion = &it->regions[static_cast<size_t>(selectedRegionIndex)];
                                           selectedRegion->sourceFilePath = result.getFullPathName();
                                           selectedRegion->sourceOffsetSeconds = 0.0;
                                           markSessionChanged(true, true);
                                       }
                                   });
}

void MainComponent::clearAudioFileFromSelectedRegion()
{
    auto* track = session.getSelectedTrack();
    if (track == nullptr)
        return;

    auto* region = session.getSelectedRegion();
    if (region == nullptr || region->kind != RegionKind::audio)
        return;

    region->sourceFilePath.clear();
    region->sourceOffsetSeconds = 0.0;
    markSessionChanged(true, true);
}

void MainComponent::loadAudioUnitIntoSelectedTrack(int slotIndex)
{
    auto* track = session.getSelectedTrack();
    if (track == nullptr)
        return;

    const auto choices = audioEngine.getAvailablePluginChoices(*track, slotIndex);
    if (choices.empty())
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(choices.size()); ++i)
        menu.addItem(i + 1, choices[static_cast<size_t>(i)].name);

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [this, trackId = track->id, slotIndex, choices] (int selectedItem)
                       {
                           if (selectedItem <= 0 || selectedItem > static_cast<int>(choices.size()))
                               return;

                           closeAudioUnitEditorWindow(trackId, slotIndex);
                           if (audioEngine.setTrackSlotPlugin(trackId, slotIndex, choices[static_cast<size_t>(selectedItem - 1)].identifier))
                               markSessionChanged(true, true);
                       });
}

void MainComponent::clearAudioUnitFromSelectedTrack(int slotIndex)
{
    auto* track = session.getSelectedTrack();
    if (track == nullptr)
        return;

    closeAudioUnitEditorWindow(track->id, slotIndex);
    audioEngine.clearTrackSlotPlugin(track->id, slotIndex);
    markSessionChanged(true, true);
}

void MainComponent::openAudioUnitEditorForSelectedTrack(int slotIndex)
{
    auto* track = session.getSelectedTrack();
    if (track == nullptr)
        return;

    closeAudioUnitEditorWindow(track->id, slotIndex);
    auto editor = audioEngine.createTrackSlotEditor(track->id, slotIndex);
    if (editor == nullptr)
        return;

    auto window = std::make_unique<PluginEditorWindow>(track->name + " / Slot " + juce::String(slotIndex + 1),
                                                       std::move(editor),
                                                       track->id,
                                                       slotIndex,
                                                       [this] (int trackId, int closedSlotIndex)
                                                       {
                                                           closeAudioUnitEditorWindow(trackId, closedSlotIndex);
                                                       });
    window->setVisible(true);
    pluginEditorWindows.push_back(std::move(window));
}

void MainComponent::closeAudioUnitEditorWindow(int trackId, int slotIndex)
{
    pluginEditorWindows.erase(std::remove_if(pluginEditorWindows.begin(),
                                             pluginEditorWindows.end(),
                                             [trackId, slotIndex] (const auto& window)
                                             {
                                                 return window != nullptr
                                                     && window->getTrackId() == trackId
                                                     && window->getSlotIndex() == slotIndex;
                                             }),
                              pluginEditorWindows.end());
}

void MainComponent::closeAllAudioUnitEditorWindows()
{
    pluginEditorWindows.clear();
}
} // namespace logiclikedaw

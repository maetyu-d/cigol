#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <juce_audio_utils/juce_audio_utils.h>

namespace cigol
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

juce::String defaultTrackNameForKind(TrackKind kind, int ordinal)
{
    switch (kind)
    {
        case TrackKind::audio: return "Audio " + juce::String(ordinal);
        case TrackKind::midi: return "MIDI " + juce::String(ordinal);
        case TrackKind::instrument: return "Instrument " + juce::String(ordinal);
        case TrackKind::superColliderRender: return "SC Render " + juce::String(ordinal);
    }

    return "Track " + juce::String(ordinal);
}

juce::String defaultTrackRoleForKind(TrackKind kind)
{
    switch (kind)
    {
        case TrackKind::audio: return "Audio Track";
        case TrackKind::midi: return "MIDI Track";
        case TrackKind::instrument: return "Instrument Track";
        case TrackKind::superColliderRender: return "SuperCollider Render";
    }

    return "Track";
}

juce::Colour defaultTrackColourForKind(TrackKind kind, int ordinal)
{
    static const std::array<juce::Colour, 4> audioColours {
        juce::Colour::fromRGB(236, 94, 90),
        juce::Colour::fromRGB(255, 133, 92),
        juce::Colour::fromRGB(255, 164, 95),
        juce::Colour::fromRGB(210, 92, 86)
    };
    static const std::array<juce::Colour, 4> midiColours {
        juce::Colour::fromRGB(247, 184, 68),
        juce::Colour::fromRGB(230, 196, 88),
        juce::Colour::fromRGB(212, 166, 57),
        juce::Colour::fromRGB(240, 150, 60)
    };
    static const std::array<juce::Colour, 4> instrumentColours {
        juce::Colour::fromRGB(67, 183, 148),
        juce::Colour::fromRGB(87, 208, 164),
        juce::Colour::fromRGB(60, 156, 128),
        juce::Colour::fromRGB(78, 194, 181)
    };
    static const std::array<juce::Colour, 4> scColours {
        juce::Colour::fromRGB(84, 155, 255),
        juce::Colour::fromRGB(112, 142, 255),
        juce::Colour::fromRGB(104, 132, 232),
        juce::Colour::fromRGB(127, 118, 255)
    };

    const auto index = static_cast<size_t>(juce::jmax(0, ordinal - 1)) % audioColours.size();
    switch (kind)
    {
        case TrackKind::audio: return audioColours[index];
        case TrackKind::midi: return midiColours[index];
        case TrackKind::instrument: return instrumentColours[index];
        case TrackKind::superColliderRender: return scColours[index];
    }

    return juce::Colours::darkgrey;
}

TrackState makeDefaultTrack(TrackKind kind, TrackChannelMode channelMode, int id, int ordinal)
{
    TrackState track;
    track.id = id;
    track.name = defaultTrackNameForKind(kind, ordinal);
    track.role = defaultTrackRoleForKind(kind);
    track.kind = kind;
    track.channelMode = kind == TrackKind::audio ? channelMode : TrackChannelMode::stereo;
    track.colour = defaultTrackColourForKind(kind, ordinal);
    track.mixer = { 0.78f, 0.0f, 0.0f };
    track.visibleAutomationLane = AutomationLaneMode::volume;
    track.automationExpanded = false;
    track.automationWriteMode = AutomationWriteMode::read;
    track.automationWriteTarget = AutomationLaneMode::none;
    track.automationGestureActive = false;
    track.automationLatchActive = false;

    if (kind == TrackKind::audio)
    {
        track.regions.push_back({ "Audio Clip", track.colour, RegionKind::audio, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
    }
    else if (kind == TrackKind::midi)
    {
        Region region { "MIDI Clip", track.colour, RegionKind::midi, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} };
        region.midiNotes = {
            { 60, 0.0, 1.0, 100, false },
            { 64, 1.0, 1.0, 100, false },
            { 67, 2.0, 1.0, 100, false }
        };
        track.regions.push_back(region);
    }
    else if (kind == TrackKind::instrument)
    {
        Region region { "Instrument Clip", track.colour, RegionKind::midi, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} };
        region.midiNotes = {
            { 48, 0.0, 2.0, 108, false },
            { 55, 0.0, 2.0, 102, false },
            { 60, 2.0, 2.0, 110, false }
        };
        track.regions.push_back(region);
    }
    else if (kind == TrackKind::superColliderRender)
    {
        track.regions.push_back({ "Generated", track.colour, RegionKind::generated, 1.0, 8.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
        track.renderScript = SuperColliderScriptState { "Scene Render",
                                                        "SinOsc.ar(220 ! 2) * 0.15",
                                                        "default",
                                                        "SynthDef(\\\\default)",
                                                        "Render -> SC Render Bus A",
                                                        "Ready",
                                                        true,
                                                        false };
    }

    return track;
}

String shortenForSidebar(const String& text, const int maxCharacters)
{
    if (text.length() <= maxCharacters || maxCharacters < 8)
        return text;

    const auto headLength = maxCharacters / 2;
    const auto tailLength = maxCharacters - headLength - 3;
    return text.substring(0, headLength) + "..." + text.substring(text.length() - tailLength);
}

std::optional<double> readAudioFileDurationSeconds(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return std::nullopt;

    return static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
}

int lowerPaneModeToStoredValue(MainComponent::LowerPaneMode mode)
{
    switch (mode)
    {
        case MainComponent::LowerPaneMode::editor: return 0;
        case MainComponent::LowerPaneMode::mixer: return 1;
        case MainComponent::LowerPaneMode::split: return 2;
    }

    return 0;
}

MainComponent::LowerPaneMode storedValueToLowerPaneMode(int value)
{
    switch (value)
    {
        case 1: return MainComponent::LowerPaneMode::mixer;
        case 2: return MainComponent::LowerPaneMode::split;
        default: return MainComponent::LowerPaneMode::editor;
    }
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

class InsertRouteMeterComponent final : public Component
{
public:
    void setLevels(float newInputLevel, float newOutputLevel, bool newActive)
    {
        inputLevel = jlimit(0.0f, 1.0f, newInputLevel);
        outputLevel = jlimit(0.0f, 1.0f, newOutputLevel);
        active = newActive;
        repaint();
    }

    void paint(Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(Colours::black.withAlpha(0.18f));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(Colours::white.withAlpha(active ? 0.12f : 0.05f));
        g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

        auto content = area.reduced(4.0f, 3.0f);
        const auto labelWidth = 10.0f;
        const auto gap = 2.0f;
        const auto rowHeight = (content.getHeight() - gap) * 0.5f;
        const auto meterActive = active;

        auto drawRow = [&g, labelWidth, meterActive] (Rectangle<float> rowArea, const String& text, float level)
        {
            auto labelArea = rowArea.removeFromLeft(labelWidth);
            g.setColour(Colours::white.withAlpha(meterActive ? 0.62f : 0.24f));
            g.setFont(FontOptions(8.0f, Font::bold));
            g.drawText(text, labelArea.toNearestInt(), Justification::centredLeft, false);

            g.setColour(Colours::white.withAlpha(meterActive ? 0.08f : 0.04f));
            g.fillRoundedRectangle(rowArea, 2.5f);

            if (level > 0.0f)
            {
                auto fill = rowArea.withWidth(rowArea.getWidth() * level);
                ColourGradient gradient(Colour::fromRGB(79, 211, 164), fill.getBottomLeft(),
                                        Colour::fromRGB(255, 152, 71), fill.getTopRight(), false);
                g.setGradientFill(gradient);
                g.fillRoundedRectangle(fill, 2.5f);
            }
        };

        auto topRow = content.removeFromTop(rowHeight);
        drawRow(topRow, "I", inputLevel);
        content.removeFromTop(gap);
        drawRow(content, "O", outputLevel);
    }

private:
    float inputLevel { 0.0f };
    float outputLevel { 0.0f };
    bool active { false };
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
                       std::function<void()> onLoadToUse,
                       std::function<void()> onAddTrackToUse,
                       std::function<void()> onRemoveTrackToUse,
                       std::function<void()> onDuplicateTrackToUse)
        : state(stateToUse),
          engine(engineToUse),
          onUndo(std::move(onUndoToUse)),
          onRedo(std::move(onRedoToUse)),
          onSave(std::move(onSaveToUse)),
          onLoad(std::move(onLoadToUse)),
          onAddTrack(std::move(onAddTrackToUse)),
          onRemoveTrack(std::move(onRemoveTrackToUse)),
          onDuplicateTrack(std::move(onDuplicateTrackToUse))
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setText("cigoL", dontSendNotification);
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
        configureButton(addTrackButton, "+ Track", [this] { if (onAddTrack != nullptr) onAddTrack(); });
        configureButton(duplicateTrackButton, "Duplicate", [this] { if (onDuplicateTrack != nullptr) onDuplicateTrack(); });
        configureButton(removeTrackButton, "- Track", [this] { if (onRemoveTrack != nullptr) onRemoveTrack(); });

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

        auto utilityButtons = area.removeFromLeft(522);
        undoButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        redoButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        saveButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        loadButton.setBounds(utilityButtons.removeFromLeft(66).reduced(4));
        addTrackButton.setBounds(utilityButtons.removeFromLeft(82).reduced(4));
        duplicateTrackButton.setBounds(utilityButtons.removeFromLeft(94).reduced(4));
        removeTrackButton.setBounds(utilityButtons.removeFromLeft(82).reduced(4));

        auto buttons = area.removeFromLeft(268);
        backButton.setBounds(buttons.removeFromLeft(48).reduced(4));
        playButton.setBounds(buttons.removeFromLeft(72).reduced(4));
        stopButton.setBounds(buttons.removeFromLeft(72).reduced(4));
        recordButton.setBounds(buttons.removeFromLeft(72).reduced(4));

        tempoSlider.setBounds(area.removeFromLeft(160).reduced(10, 4));
        positionLabel.setBounds(area.removeFromRight(190));
    }

    void setProjectStatus(const String& projectName,
                          const bool dirty,
                          const bool canUndo,
                          const bool canRedo,
                          const bool canRemoveTrack,
                          const bool canDuplicateTrack)
    {
        projectLabel.setText((dirty ? "* " : "") + projectName, dontSendNotification);
        undoButton.setEnabled(canUndo);
        redoButton.setEnabled(canRedo);
        removeTrackButton.setEnabled(canRemoveTrack);
        duplicateTrackButton.setEnabled(canDuplicateTrack);
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
    std::function<void()> onAddTrack;
    std::function<void()> onRemoveTrack;
    std::function<void()> onDuplicateTrack;
    Label titleLabel;
    Label statusLabel;
    Label projectLabel;
    Label positionLabel;
    TextButton undoButton;
    TextButton redoButton;
    TextButton saveButton;
    TextButton loadButton;
    TextButton addTrackButton;
    TextButton duplicateTrackButton;
    TextButton removeTrackButton;
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
        const auto formatSuffix = track.kind == TrackKind::audio ? " / " + toDisplayString(track.channelMode) : juce::String();
        roleLabel.setText(track.role + " / " + toDisplayString(track.kind) + formatSuffix, dontSendNotification);
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
        auto lane = track.visibleAutomationLane == AutomationLaneMode::pan ? "P"
            : (track.visibleAutomationLane == AutomationLaneMode::plugin ? "Fx" : "A");
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
                         int initialHeaderWidth,
                         std::function<void(int)> onTrackSelectToUse,
                         std::function<void(int, int)> onRegionSelectToUse,
                         std::function<void()> onRegionEditToUse,
                         std::function<void()> onAutomationLayoutChangeToUse,
                         std::function<void()> onTrackEditToUse,
                         std::function<void(int)> onHeaderWidthChangedToUse)
        : session(sessionToUse),
          headerWidth(juce::jlimit(minimumHeaderWidth, maximumHeaderWidth, initialHeaderWidth)),
          onTrackSelect(std::move(onTrackSelectToUse)),
          onRegionSelect(std::move(onRegionSelectToUse)),
          onRegionEdit(std::move(onRegionEditToUse)),
          onAutomationLayoutChange(std::move(onAutomationLayoutChangeToUse)),
          onTrackEdit(std::move(onTrackEditToUse)),
          onHeaderWidthChanged(std::move(onHeaderWidthChangedToUse)),
          thumbnailCache(24)
    {
        audioFormatManager.registerBasicFormats();
        setWantsKeyboardFocus(true);
        rebuildTrackHeaders();
    }

    void setHeaderWidth(int newWidth)
    {
        headerWidth = juce::jlimit(minimumHeaderWidth, maximumHeaderWidth, newWidth);
        if (onHeaderWidthChanged != nullptr)
            onHeaderWidthChanged(headerWidth);
        resized();
        repaint();
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

                for (int laneIndex = 0; laneIndex < automationLaneRowCount(track); ++laneIndex)
                {
                    auto laneRow = automationLaneBounds(i, laneIndex);
                    if (laneIndex > 0)
                    {
                        g.setColour(Colours::white.withAlpha(0.04f));
                        g.drawLine(static_cast<float>(laneRow.getX()), static_cast<float>(laneRow.getY()),
                                   static_cast<float>(laneRow.getRight()), static_cast<float>(laneRow.getY()));
                    }

                    paintAutomationLane(g, laneRow, track, laneIndex);
                }
            }
        }

        g.setColour(Colours::white.withAlpha(0.10f));
        g.fillRect(headerWidth - 1, timelineHeight, 2, getHeight() - timelineHeight);
        g.setColour(Colours::white.withAlpha(0.24f));
        g.fillRoundedRectangle(Rectangle<float>(static_cast<float>(headerWidth - 2),
                                                static_cast<float>(timelineHeight + 16),
                                                4.0f,
                                                64.0f),
                               2.0f);

        paintPlayhead(g, gridArea);
    }

    void resized() override
    {
        for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i)
            trackHeaders[static_cast<size_t>(i)]->setBounds(0, trackRowBounds(i).getY(), headerWidth, trackRowBounds(i).getHeight());
    }

    void mouseUp(const MouseEvent& event) override
    {
        if (draggingHeaderSplitter)
        {
            draggingHeaderSplitter = false;
            return;
        }

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
            if (track.visibleAutomationLane == AutomationLaneMode::plugin)
                track.selectedPluginAutomationLaneIndex = automationLaneIndexAtY(track, event.y);
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

        if (std::abs(event.x - headerWidth) <= 6)
        {
            draggingHeaderSplitter = true;
            headerWidthAtDragStart = headerWidth;
            return;
        }

        if (const auto automationHit = hitTestAutomationPoint(event.getPosition()); automationHit.trackId >= 0)
        {
            if (onTrackSelect != nullptr)
                onTrackSelect(automationHit.trackId);

            selectedAutomationTrackId = automationHit.trackId;
            selectedAutomationPointIndex = automationHit.pointIndex;
            selectedAutomationLaneIndex = automationHit.laneIndex;

            dragState.active = true;
            dragState.mode = DragMode::automationPoint;
            dragState.trackId = automationHit.trackId;
            dragState.automationPointIndex = automationHit.pointIndex;
            dragState.automationLaneIndex = automationHit.laneIndex;
            dragState.dragStartPoint = event.getPosition();

            if (auto* track = getTrackById(automationHit.trackId))
            {
                if (track->visibleAutomationLane == AutomationLaneMode::plugin)
                    track->selectedPluginAutomationLaneIndex = automationHit.laneIndex;

                const auto& point = getAutomationPoints(*track, automationHit.laneIndex)[static_cast<size_t>(automationHit.pointIndex)];
                dragState.originalAutomationBeat = point.beat;
                dragState.originalAutomationValue = point.value;
            }

            return;
        }

        if (const auto target = hitTestRegion(event.getPosition()); target.trackId >= 0)
        {
            selectedAutomationTrackId = -1;
            selectedAutomationPointIndex = -1;
            selectedAutomationLaneIndex = 0;
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
        if (std::abs(event.x - headerWidth) <= 6)
        {
            setHeaderWidth(headerWidth <= collapsedHeaderWidth ? 240 : collapsedHeaderWidth);
            return;
        }

        if (event.x < headerWidth || event.y < timelineHeight)
            return;

        if (const auto automationHit = hitTestAutomationPoint(event.getPosition()); automationHit.trackId >= 0)
        {
            if (auto* track = getTrackById(automationHit.trackId))
            {
                if (track->visibleAutomationLane == AutomationLaneMode::plugin)
                    track->selectedPluginAutomationLaneIndex = automationHit.laneIndex;

                auto& points = getAutomationPoints(*track, automationHit.laneIndex);
                if (automationHit.pointIndex >= 0 && automationHit.pointIndex < static_cast<int>(points.size()) - 1)
                {
                    auto& point = points[static_cast<size_t>(automationHit.pointIndex)];
                    point.shapeToNext = nextSegmentShape(point.shapeToNext);
                    selectedAutomationTrackId = automationHit.trackId;
                    selectedAutomationPointIndex = automationHit.pointIndex;
                    selectedAutomationLaneIndex = automationHit.laneIndex;

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
        selectedAutomationLaneIndex = automationLaneIndexAtY(track, event.y);
        if (track.visibleAutomationLane == AutomationLaneMode::plugin)
            track.selectedPluginAutomationLaneIndex = selectedAutomationLaneIndex;

        AutomationPoint point;
        point.beat = juce::jlimit(1.0, session.transport.visibleBeats, xPositionToBeat(event.x));
        point.value = yPositionToAutomationValue(automationLaneBounds(trackRow, selectedAutomationLaneIndex), event.y, track.visibleAutomationLane);
        auto& points = getAutomationPoints(track, selectedAutomationLaneIndex);
        points.push_back(point);
        sortAutomation(points);
        selectedAutomationPointIndex = indexOfAutomationPoint(track, point.beat, point.value, selectedAutomationLaneIndex);

        if (onRegionEdit != nullptr)
            onRegionEdit();

        repaint();
    }

    void mouseDrag(const MouseEvent& event) override
    {
        if (draggingHeaderSplitter)
        {
            auto proposedWidth = headerWidthAtDragStart + event.getDistanceFromDragStartX();
            if (proposedWidth < 72)
                proposedWidth = collapsedHeaderWidth;

            setHeaderWidth(proposedWidth);
            return;
        }

        if (! dragState.active)
            return;

        if (dragState.mode == DragMode::automationPoint)
        {
            if (auto* track = getTrackById(dragState.trackId))
            {
                auto& points = getAutomationPoints(*track, dragState.automationLaneIndex);
                if (dragState.automationPointIndex >= 0
                    && dragState.automationPointIndex < static_cast<int>(points.size()))
                {
                    const auto trackRow = trackRowForId(dragState.trackId);
                    if (trackRow >= 0)
                    {
                        if (track->visibleAutomationLane == AutomationLaneMode::plugin)
                            track->selectedPluginAutomationLaneIndex = dragState.automationLaneIndex;
                        auto& draggedPoint = points[static_cast<size_t>(dragState.automationPointIndex)];
                        draggedPoint.beat = juce::jlimit(1.0, session.transport.visibleBeats,
                                                         dragState.originalAutomationBeat + snapBeatDelta(xDeltaToBeatDelta(event.getDistanceFromDragStartX())));
                        draggedPoint.value = yPositionToAutomationValue(automationLaneBounds(trackRow, dragState.automationLaneIndex),
                                                                        event.y,
                                                                        track->visibleAutomationLane);
                        sortAutomation(points);
                        selectedAutomationPointIndex = nearestAutomationPointIndex(*track, draggedPoint.beat, draggedPoint.value, dragState.automationLaneIndex);
                        selectedAutomationLaneIndex = dragState.automationLaneIndex;
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
                auto& points = getAutomationPoints(*track, selectedAutomationLaneIndex);
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

    void mouseMove(const MouseEvent& event) override
    {
        if (std::abs(event.x - headerWidth) <= 6)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

private:
    static constexpr int minimumHeaderWidth = 44;
    static constexpr int maximumHeaderWidth = 420;
    static constexpr int collapsedHeaderWidth = 44;
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
        int laneIndex { 0 };
    };

    struct DragState
    {
        bool active { false };
        DragMode mode { DragMode::none };
        RegionHandle handle { RegionHandle::none };
        int trackId { -1 };
        int regionIndex { -1 };
        int automationPointIndex { -1 };
        int automationLaneIndex { 0 };
        Point<int> dragStartPoint;
        double originalStartBeat { 0.0 };
        double originalLengthInBeats { 0.0 };
        double originalSourceOffsetSeconds { 0.0 };
        double originalFadeInBeats { 0.0 };
        double originalFadeOutBeats { 0.0 };
        double originalAutomationBeat { 1.0 };
        float originalAutomationValue { 1.0f };
    };

    int headerWidth { 240 };
    bool draggingHeaderSplitter { false };
    int headerWidthAtDragStart { 240 };

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

    void paintAutomationLane(Graphics& g, Rectangle<int> row, const TrackState& track, int laneIndex)
    {
        const auto laneMode = track.visibleAutomationLane;
        const auto baselineValue = laneMode == AutomationLaneMode::pan ? track.mixer.pan
            : (laneMode == AutomationLaneMode::plugin ? 0.5f : track.mixer.volume);
        g.setColour(track.selected ? track.colour.withAlpha(0.20f) : Colours::white.withAlpha(0.06f));
        const auto baseline = yForAutomationValue(row, baselineValue, laneMode);
        g.drawLine(static_cast<float>(row.getX() + 6), baseline,
                   static_cast<float>(row.getRight() - 6), baseline, 1.0f);

        auto points = getAutomationPointsCopy(track, laneIndex);

        if (points.empty())
            points.push_back({ 1.0, baselineValue });

        g.setColour(track.colour.withAlpha(track.selected ? 0.95f : 0.70f));
        juce::Path automationPath;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto x = xForBeat(points[i].beat);
            const auto y = yForAutomationValue(row, points[i].value, laneMode);

            if (i == 0)
                automationPath.startNewSubPath(x, y);

            if (i + 1 < points.size())
            {
                const auto& next = points[i + 1];
                appendAutomationSegment(automationPath, row, points[i], next, laneMode);

                if (track.selected)
                {
                    const auto segmentMidX = xForBeat((points[i].beat + next.beat) * 0.5);
                    const auto segmentMidValue = interpolateAutomationDisplayValue({ points[i], next }, (points[i].beat + next.beat) * 0.5, points[i].value);
                    const auto segmentMidY = yForAutomationValue(row, segmentMidValue, laneMode);
                    g.setColour(Colours::white.withAlpha(0.38f));
                    g.drawText(shapeShortLabel(points[i].shapeToNext),
                               juce::Rectangle<int>(static_cast<int>(segmentMidX) - 18, static_cast<int>(segmentMidY) - 18, 36, 12),
                               Justification::centred, false);
                    g.setColour(track.colour.withAlpha(track.selected ? 0.95f : 0.70f));
                }
            }

            const auto pointSelected = track.id == selectedAutomationTrackId
                && laneIndex == selectedAutomationLaneIndex
                && static_cast<int>(i) == selectedAutomationPointIndex;
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
        juce::String laneLabel = toDisplayString(laneMode) + " automation";
        if (laneMode == AutomationLaneMode::plugin
            && laneIndex >= 0
            && laneIndex < static_cast<int>(track.pluginAutomationLanes.size()))
        {
            const auto& lane = track.pluginAutomationLanes[static_cast<size_t>(laneIndex)];
            laneLabel = lane.displayName.isNotEmpty()
                ? lane.displayName
                : "S" + juce::String(lane.slotIndex + 1) + " / " + lane.parameterName;
            laneLabel = shortenForSidebar(laneLabel, 26);
        }
        g.drawText(laneLabel, row.removeFromLeft(170).reduced(8, 4), Justification::centredLeft, false);
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
        return automationLaneRowCount(track) * automationLaneHeight;
    }

    int automationLaneRowCount(const TrackState& track) const
    {
        if (track.visibleAutomationLane == AutomationLaneMode::none || ! track.automationExpanded)
            return 0;

        if (track.visibleAutomationLane == AutomationLaneMode::plugin)
            return juce::jmax(1, static_cast<int>(track.pluginAutomationLanes.size()));

        return 1;
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

    Rectangle<int> automationLaneBounds(int trackRow, int laneIndex) const
    {
        auto row = trackAutomationBounds(trackRow);
        const auto laneCount = automationLaneRowCount(session.tracks[static_cast<size_t>(trackRow)]);
        if (laneCount <= 0)
            return {};

        laneIndex = juce::jlimit(0, laneCount - 1, laneIndex);
        row.removeFromTop(laneIndex * automationLaneHeight);
        return row.removeFromTop(automationLaneHeight);
    }

    int automationLaneIndexAtY(const TrackState& track, int y) const
    {
        if (track.visibleAutomationLane != AutomationLaneMode::plugin)
            return 0;

        const auto trackRow = trackRowForId(track.id);
        if (trackRow < 0)
            return 0;

        const auto area = trackAutomationBounds(trackRow);
        return juce::jlimit(0,
                            juce::jmax(0, automationLaneRowCount(track) - 1),
                            (y - area.getY()) / juce::jmax(1, automationLaneHeight));
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

        const auto laneIndex = automationLaneIndexAtY(track, point.y);
        const auto laneRow = automationLaneBounds(trackRow, laneIndex);
        const auto& points = getAutomationPoints(track, laneIndex);
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto pointX = xForBeat(points[i].beat);
            const auto pointY = yForAutomationValue(laneRow, points[i].value, track.visibleAutomationLane);
            juce::Rectangle<float> pointBounds(pointX - 7.0f, pointY - 7.0f, 14.0f, 14.0f);

            if (pointBounds.contains(static_cast<float>(point.x), static_cast<float>(point.y)))
                return { track.id, static_cast<int>(i), laneIndex };
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
        const auto laneIndex = track.visibleAutomationLane == AutomationLaneMode::plugin ? track.selectedPluginAutomationLaneIndex : 0;
        return getAutomationPoints(track, laneIndex);
    }

    std::vector<AutomationPoint>& getAutomationPoints(TrackState& track, int laneIndex)
    {
        if (track.visibleAutomationLane == AutomationLaneMode::pan)
            return track.panAutomation;
        if (track.visibleAutomationLane == AutomationLaneMode::plugin)
        {
            static std::vector<AutomationPoint> emptyPoints;
            if (laneIndex >= 0
                && laneIndex < static_cast<int>(track.pluginAutomationLanes.size()))
                return track.pluginAutomationLanes[static_cast<size_t>(laneIndex)].points;
            return emptyPoints;
        }
        return track.volumeAutomation;
    }

    const std::vector<AutomationPoint>& getAutomationPoints(const TrackState& track) const
    {
        const auto laneIndex = track.visibleAutomationLane == AutomationLaneMode::plugin ? track.selectedPluginAutomationLaneIndex : 0;
        return getAutomationPoints(track, laneIndex);
    }

    const std::vector<AutomationPoint>& getAutomationPoints(const TrackState& track, int laneIndex) const
    {
        if (track.visibleAutomationLane == AutomationLaneMode::pan)
            return track.panAutomation;
        if (track.visibleAutomationLane == AutomationLaneMode::plugin)
        {
            static const std::vector<AutomationPoint> emptyPoints;
            if (laneIndex >= 0
                && laneIndex < static_cast<int>(track.pluginAutomationLanes.size()))
                return track.pluginAutomationLanes[static_cast<size_t>(laneIndex)].points;
            return emptyPoints;
        }
        return track.volumeAutomation;
    }

    std::vector<AutomationPoint> getAutomationPointsCopy(const TrackState& track) const
    {
        return getAutomationPoints(track);
    }

    std::vector<AutomationPoint> getAutomationPointsCopy(const TrackState& track, int laneIndex) const
    {
        return getAutomationPoints(track, laneIndex);
    }

    float automationMinimumValue(const TrackState& track) const
    {
        return track.visibleAutomationLane == AutomationLaneMode::pan ? -1.0f : 0.0f;
    }

    float automationMaximumValue(const TrackState&) const
    {
        return 1.0f;
    }

    int nearestAutomationPointIndex(const TrackState& track, double beat, float value, int laneIndex) const
    {
        const auto& points = getAutomationPoints(track, laneIndex);
        for (size_t i = 0; i < points.size(); ++i)
            if (std::abs(points[i].beat - beat) < 0.001 && std::abs(points[i].value - value) < 0.001f)
                return static_cast<int>(i);
        return -1;
    }

    int indexOfAutomationPoint(const TrackState& track, double beat, float value, int laneIndex) const
    {
        return nearestAutomationPointIndex(track, beat, value, laneIndex);
    }

    SessionState& session;
    std::function<void(int)> onTrackSelect;
    std::function<void(int, int)> onRegionSelect;
    std::function<void()> onRegionEdit;
    std::function<void()> onAutomationLayoutChange;
    std::function<void()> onTrackEdit;
    std::function<void(int)> onHeaderWidthChanged;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> waveformCache;
    std::vector<std::unique_ptr<TrackHeaderComponent>> trackHeaders;
    DragState dragState;
    int selectedAutomationTrackId { -1 };
    int selectedAutomationPointIndex { -1 };
    int selectedAutomationLaneIndex { 0 };
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

    void zoomIn()
    {
        session.layout.midiEditorZoom = juce::jlimit(1.0f, 8.0f, session.layout.midiEditorZoom * 1.25f);
        repaint();
    }

    void zoomOut()
    {
        session.layout.midiEditorZoom = juce::jlimit(1.0f, 8.0f, session.layout.midiEditorZoom / 1.25f);
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

        if (session.layout.midiEditorTool == 1)
        {
            clearNoteSelection(*region);
            Region::MidiNote note;
            note.pitch = pitchForY(layout.gridArea, event.y);
            note.startBeat = snapBeat(beatForX(layout.gridArea, event.x));
            note.lengthInBeats = 1.0;
            note.velocity = 100;
            note.selected = true;
            note.lengthInBeats = juce::jmin(note.lengthInBeats, juce::jmax(minimumNoteLength, visibleBeatSpan(*region) - note.startBeat));
            region->midiNotes.push_back(note);
            dragState.active = true;
            dragState.noteIndex = static_cast<int>(region->midiNotes.size()) - 1;
            dragState.mode = NoteHandle::right;
            dragState.dragStart = event.getPosition();
            dragState.originalStartBeat = note.startBeat;
            dragState.originalLengthBeats = note.lengthInBeats;
            dragState.originalPitch = note.pitch;
            if (onEdit != nullptr)
                onEdit();
            repaint();
            return;
        }

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
                   header.removeFromLeft(220), Justification::centredLeft, false);
        g.setColour(Colours::white.withAlpha(0.42f));
        g.setFont(FontOptions(12.0f, Font::bold));
        g.drawText("Zoom " + String(session.layout.midiEditorZoom, 2) + "x", header, Justification::centredRight, false);
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

        const auto visibleBeats = visibleBeatSpan(region);
        const auto beatWidth = static_cast<float>(area.getWidth()) / static_cast<float>(juce::jmax(1.0, visibleBeats));
        const auto beatCount = static_cast<int>(std::ceil(visibleBeats));

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
            layout.beatWidth = static_cast<float>(layout.gridArea.getWidth()) / static_cast<float>(juce::jmax(1.0, visibleBeatSpan(*region)));

        return layout;
    }

    Rectangle<int> boundsForNote(Rectangle<int> gridArea, const Region& region, const Region::MidiNote& note) const
    {
        const auto beatWidth = static_cast<float>(gridArea.getWidth()) / static_cast<float>(juce::jmax(1.0, visibleBeatSpan(region)));
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
            return juce::jlimit(0.0, juce::jmax(0.0, visibleBeatSpan(*region) - minimumNoteLength), normalised * visibleBeatSpan(*region));
        return 0.0;
    }

    double visibleBeatSpan(const Region& region) const
    {
        return juce::jmax(1.0, region.lengthInBeats / juce::jlimit(1.0f, 8.0f, session.layout.midiEditorZoom));
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

class MainComponent::AudioClipEditorComponent final : public Component
{
public:
    explicit AudioClipEditorComponent(SessionState& sessionToUse)
        : session(sessionToUse),
          thumbnailCache(16)
    {
        audioFormatManager.registerBasicFormats();
    }

    void refresh()
    {
        repaint();
    }

    void zoomIn()
    {
        session.layout.audioEditorZoom = juce::jlimit(1.0f, 8.0f, session.layout.audioEditorZoom * 1.25f);
        repaint();
    }

    void zoomOut()
    {
        session.layout.audioEditorZoom = juce::jlimit(1.0f, 8.0f, session.layout.audioEditorZoom / 1.25f);
        repaint();
    }

    void paint(Graphics& g) override
    {
        auto area = getLocalBounds();
        g.fillAll(Colour::fromRGB(22, 25, 32));
        g.setColour(Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 10.0f, 1.0f);

        auto content = area.reduced(12);
        auto header = content.removeFromTop(28);

        const auto* region = session.getSelectedRegion();
        if (region == nullptr || region->kind != RegionKind::audio)
        {
            g.setColour(Colours::white.withAlpha(0.88f));
            g.setFont(FontOptions(15.0f, Font::bold));
            g.drawText("Audio File Editor", header, Justification::centredLeft, false);
            g.setColour(Colours::white.withAlpha(0.52f));
            g.setFont(FontOptions(13.0f));
            g.drawText("Select an audio region to open the file editor.", content, Justification::centred, true);
            return;
        }

        g.setColour(Colours::white.withAlpha(0.90f));
        g.setFont(FontOptions(15.0f, Font::bold));
        g.drawText("Audio File Editor", header.removeFromLeft(140), Justification::centredLeft, false);
        g.setColour(Colours::white.withAlpha(0.58f));
        g.setFont(FontOptions(13.0f));
        g.drawText(region->name + " / " + juce::String(region->lengthInBeats, 2) + " beats",
                   header.removeFromLeft(220), Justification::centredLeft, false);
        g.setColour(Colours::white.withAlpha(0.42f));
        g.setFont(FontOptions(12.0f, Font::bold));
        g.drawText("Zoom " + String(session.layout.audioEditorZoom, 2) + "x", header, Justification::centredRight, false);

        auto waveformArea = content.removeFromTop(juce::jmax(80, content.getHeight() - 54));
        auto footerArea = content;

        g.setColour(Colour::fromRGB(27, 31, 39));
        g.fillRoundedRectangle(waveformArea.toFloat(), 8.0f);

        if (region->sourceFilePath.isEmpty())
        {
            g.setColour(Colours::white.withAlpha(0.50f));
            g.drawText("No file assigned to this audio region.", waveformArea, Justification::centred, true);
        }
        else if (auto* thumbnail = getThumbnailForFile(region->sourceFilePath))
        {
            auto drawArea = waveformArea.reduced(12, 14);
            g.setColour(Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(drawArea.toFloat(), 6.0f);

            const auto regionDurationSeconds = juce::jmax(0.01, region->lengthInBeats * 60.0 / juce::jmax(1.0, session.transport.bpm));
            const auto visibleDurationSeconds = juce::jmax(0.01, regionDurationSeconds / juce::jlimit(1.0f, 8.0f, session.layout.audioEditorZoom));
            const auto startSeconds = juce::jmax(0.0, region->sourceOffsetSeconds);
            const auto endSeconds = juce::jmin(thumbnail->getTotalLength(), startSeconds + visibleDurationSeconds);

            g.setColour(region->colour.withAlpha(0.18f));
            g.fillRoundedRectangle(drawArea.toFloat(), 6.0f);
            thumbnail->drawChannels(g,
                                    drawArea,
                                    startSeconds,
                                    juce::jmax(startSeconds + 0.001, endSeconds),
                                    1.0f);

            const auto fadeInWidth = static_cast<float>(drawArea.getWidth()) * static_cast<float>(region->fadeInBeats / juce::jmax(0.001, region->lengthInBeats));
            const auto fadeOutWidth = static_cast<float>(drawArea.getWidth()) * static_cast<float>(region->fadeOutBeats / juce::jmax(0.001, region->lengthInBeats));
            g.setColour(Colours::white.withAlpha(0.32f));
            g.drawLine(drawArea.getX() + 8.0f,
                       static_cast<float>(drawArea.getBottom() - 10),
                       drawArea.getX() + juce::jmax(8.0f, fadeInWidth),
                       static_cast<float>(drawArea.getY() + 10),
                       1.4f);
            g.drawLine(static_cast<float>(drawArea.getRight()) - juce::jmax(8.0f, fadeOutWidth),
                       static_cast<float>(drawArea.getY() + 10),
                       static_cast<float>(drawArea.getRight()) - 8.0f,
                       static_cast<float>(drawArea.getBottom() - 10),
                       1.4f);

            g.setColour(Colours::white.withAlpha(0.48f));
            g.setFont(FontOptions(11.0f, Font::bold));
            g.drawText("Start " + juce::String(region->sourceOffsetSeconds, 2) + "s",
                       drawArea.removeFromBottom(16), Justification::centredLeft, false);
        }
        else
        {
            g.setColour(Colours::white.withAlpha(0.50f));
            g.drawText("The assigned file could not be read.", waveformArea, Justification::centred, true);
        }

        auto chipArea = footerArea.removeFromTop(20);
        paintChip(g, chipArea.removeFromLeft(126), "Fade In", juce::String(region->fadeInBeats, 2) + " beats");
        chipArea.removeFromLeft(8);
        paintChip(g, chipArea.removeFromLeft(132), "Fade Out", juce::String(region->fadeOutBeats, 2) + " beats");
        chipArea.removeFromLeft(8);
        paintChip(g, chipArea.removeFromLeft(120), "Gain", juce::String(region->gain, 2) + "x");
        footerArea.removeFromTop(6);
        g.setColour(Colours::white.withAlpha(0.52f));
        g.setFont(FontOptions(12.0f));
        g.drawText(region->sourceFilePath.isNotEmpty()
                       ? shortenForSidebar(region->sourceFilePath, 72)
                       : "No source file",
                   footerArea, Justification::centredLeft, false);
    }

    void mouseDown(const MouseEvent& event) override
    {
        if (session.layout.audioEditorTool != 1)
            return;

        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(28);
        const auto waveformArea = area.removeFromTop(juce::jmax(80, area.getHeight() - 54));
        if (! waveformArea.contains(event.getPosition()))
            return;

        zoomIn();
    }

    void mouseDoubleClick(const MouseEvent&) override
    {
        session.layout.audioEditorZoom = 1.0f;
        repaint();
    }

private:
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

    void paintChip(Graphics& g, Rectangle<int> bounds, const String& title, const String& value) const
    {
        g.setColour(Colour::fromRGB(34, 39, 49));
        g.fillRoundedRectangle(bounds.toFloat(), 7.0f);
        g.setColour(Colours::white.withAlpha(0.12f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 7.0f, 1.0f);
        auto inner = bounds.reduced(8, 4);
        g.setColour(Colours::white.withAlpha(0.48f));
        g.setFont(FontOptions(10.0f, Font::bold));
        g.drawText(title.toUpperCase(), inner.removeFromTop(10), Justification::centredLeft, false);
        g.setColour(Colours::white.withAlpha(0.86f));
        g.setFont(FontOptions(12.0f));
        g.drawText(value, inner, Justification::centredLeft, false);
    }

    SessionState& session;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> waveformCache;
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
        pluginValueLabel.setFont(FontOptions(12.0f, Font::bold));
        pluginLaneNameLabel.setFont(FontOptions(12.0f, Font::bold));
        pluginLaneStatusLabel.setFont(FontOptions(12.0f));
        volumeLabel.setFont(FontOptions(12.0f, Font::bold));
        panLabel.setFont(FontOptions(12.0f, Font::bold));
        scInsertStatusLabel.setFont(FontOptions(12.0f));
        scInsertMetersLabel.setFont(FontOptions(12.0f, Font::bold));
        scInsertWetLabel.setFont(FontOptions(12.0f, Font::bold));
        scInsertTrimLabel.setFont(FontOptions(12.0f, Font::bold));
        automationModeLabel.setFont(FontOptions(12.0f, Font::bold));
        automationValueLabel.setFont(FontOptions(12.0f));
        automationWriteLabel.setFont(FontOptions(12.0f, Font::bold));
        pluginParameterLabel.setFont(FontOptions(12.0f, Font::bold));
        pluginLaneActionsLabel.setFont(FontOptions(12.0f, Font::bold));
        superColliderLabel.setFont(FontOptions(13.0f));
        processorLabel.setFont(FontOptions(13.0f));
        slotOneLabel.setFont(FontOptions(12.0f));
        slotTwoLabel.setFont(FontOptions(12.0f));

        addAndMakeVisible(titleLabel);
        addAndMakeVisible(clipSectionToggleButton);
        addAndMakeVisible(trackSectionToggleButton);
        addAndMakeVisible(channelSectionToggleButton);
        addAndMakeVisible(trackNameLabel);
        addAndMakeVisible(roleLabel);
        addAndMakeVisible(audioRegionLabel);
        addAndMakeVisible(audioFileLabel);
        addAndMakeVisible(regionGainLabel);
        addAndMakeVisible(regionGainSlider);
        addAndMakeVisible(pluginValueLabel);
        addAndMakeVisible(pluginValueSlider);
        addAndMakeVisible(pluginLaneNameLabel);
        addAndMakeVisible(pluginLaneNameEditor);
        addAndMakeVisible(pluginLaneStatusLabel);
        addAndMakeVisible(scInsertStatusLabel);
        addAndMakeVisible(scInsertMetersLabel);
        addAndMakeVisible(scInsertWetLabel);
        addAndMakeVisible(scInsertWetSlider);
        addAndMakeVisible(scInsertTrimLabel);
        addAndMakeVisible(scInsertTrimSlider);
        addAndMakeVisible(scInsertBypassButton);
        addAndMakeVisible(volumeLabel);
        addAndMakeVisible(panLabel);
        addAndMakeVisible(automationModeLabel);
        addAndMakeVisible(automationValueLabel);
        addAndMakeVisible(automationWriteLabel);
        addAndMakeVisible(pluginParameterLabel);
        addAndMakeVisible(pluginParameterBox);
        addAndMakeVisible(addPluginAutomationButton);
        addAndMakeVisible(pluginLaneActionsLabel);
        addAndMakeVisible(movePluginLaneUpButton);
        addAndMakeVisible(movePluginLaneDownButton);
        addAndMakeVisible(removePluginLaneButton);
        addAndMakeVisible(assignAudioButton);
        addAndMakeVisible(clearAudioButton);
        addAndMakeVisible(showVolumeAutomationButton);
        addAndMakeVisible(showPanAutomationButton);
        addAndMakeVisible(showPluginAutomationButton);
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
        addAndMakeVisible(slotOneMeterBadge);
        addAndMakeVisible(loadSlotTwoButton);
        addAndMakeVisible(openSlotTwoButton);
        addAndMakeVisible(clearSlotTwoButton);
        addAndMakeVisible(slotTwoMeterBadge);
        addAndMakeVisible(volumeSlider);
        addAndMakeVisible(panSlider);

        auto configureSectionToggle = [] (TextButton& button)
        {
            button.setColour(TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
            button.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.88f));
        };
        configureSectionToggle(clipSectionToggleButton);
        configureSectionToggle(trackSectionToggleButton);
        configureSectionToggle(channelSectionToggleButton);
        clipSectionToggleButton.onClick = [this] { clipSectionExpanded = ! clipSectionExpanded; updateSectionToggleButtons(); resized(); repaint(); };
        trackSectionToggleButton.onClick = [this] { trackSectionExpanded = ! trackSectionExpanded; updateSectionToggleButtons(); resized(); repaint(); };
        channelSectionToggleButton.onClick = [this] { channelSectionExpanded = ! channelSectionExpanded; updateSectionToggleButtons(); resized(); repaint(); };
        updateSectionToggleButtons();

        volumeLabel.setText("Volume", dontSendNotification);
        panLabel.setText("Pan", dontSendNotification);
        automationModeLabel.setText("Automation lane", dontSendNotification);
        automationWriteLabel.setText("Write automation", dontSendNotification);
        pluginParameterLabel.setText("Plugin parameter", dontSendNotification);
        pluginValueLabel.setText("Parameter value", dontSendNotification);
        pluginLaneNameLabel.setText("Lane name", dontSendNotification);
        pluginLaneStatusLabel.setText({}, dontSendNotification);
        scInsertMetersLabel.setText("SC insert", dontSendNotification);
        scInsertWetLabel.setText("Wet / Dry", dontSendNotification);
        scInsertTrimLabel.setText("Output trim", dontSendNotification);
        scInsertBypassButton.setButtonText("SC Bypass");
        pluginLaneActionsLabel.setText("Lane actions", dontSendNotification);
        addPluginAutomationButton.setButtonText("Add");
        addPluginAutomationButton.setColour(TextButton::buttonColourId, Colour::fromRGB(67, 73, 88));
        addPluginAutomationButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.90f));
        movePluginLaneUpButton.setButtonText("Up");
        movePluginLaneDownButton.setButtonText("Down");
        removePluginLaneButton.setButtonText("Remove");
        duplicatePluginLaneButton.setButtonText("Duplicate");
        remapPluginLaneButton.setButtonText("Remap");
        for (auto* button : { &movePluginLaneUpButton, &movePluginLaneDownButton, &removePluginLaneButton, &duplicatePluginLaneButton, &remapPluginLaneButton })
        {
            button->setColour(TextButton::buttonColourId, Colour::fromRGB(67, 73, 88));
            button->setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.90f));
        }
        scInsertBypassButton.setColour(TextButton::buttonColourId, Colour::fromRGB(67, 73, 88));
        scInsertBypassButton.setColour(TextButton::textColourOffId, Colours::white.withAlpha(0.90f));
        scInsertBypassButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto slotIndex = findSuperColliderInsertSlot(*track);
                if (slotIndex >= 0)
                {
                    const auto bypassed = ! track->inserts[static_cast<size_t>(slotIndex)].bypassed;
                    if (audioEngine.setTrackSlotBypassed(track->id, slotIndex, bypassed))
                    {
                        refresh();
                        if (onAutomationEdited != nullptr)
                            onAutomationEdited();
                    }
                }
            }
        };
        pluginLaneNameEditor.setColour(juce::TextEditor::backgroundColourId, Colour::fromRGB(31, 36, 46));
        pluginLaneNameEditor.setColour(juce::TextEditor::textColourId, Colours::white.withAlpha(0.92f));
        pluginLaneNameEditor.setColour(juce::TextEditor::outlineColourId, Colours::white.withAlpha(0.10f));
        pluginLaneNameEditor.setColour(juce::TextEditor::focusedOutlineColourId, Colour::fromRGB(96, 124, 172));
        pluginLaneNameEditor.onTextChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
                if (auto* pluginLane = getSelectedPluginAutomationLane(*track))
                {
                    pluginLane->displayName = pluginLaneNameEditor.getText().trim();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
        };

        pluginValueSlider.setSliderStyle(Slider::LinearHorizontal);
        pluginValueSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        pluginValueSlider.setRange(0.0, 1.0, 0.001);
        pluginValueSlider.onValueChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
                if (auto* pluginLane = getSelectedPluginAutomationLane(*track))
                {
                    const auto value = static_cast<float>(pluginValueSlider.getValue());
                    audioEngine.setTrackPluginParameterValue(track->id, pluginLane->slotIndex, pluginLane->parameterIndex, value);
                    writeAutomationPoint(*track, AutomationLaneMode::plugin, value);
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
        };
        pluginValueSlider.onDragStart = [this] { beginAutomationGesture(AutomationLaneMode::plugin); };
        pluginValueSlider.onDragEnd = [this] { endAutomationGesture(AutomationLaneMode::plugin); };

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
        pluginValueLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        pluginLaneNameLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        pluginLaneStatusLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.56f));
        scInsertStatusLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.56f));
        scInsertMetersLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        scInsertWetLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        scInsertTrimLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        pluginLaneActionsLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        volumeLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        panLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        automationModeLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        automationValueLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.56f));
        automationWriteLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        pluginParameterLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.62f));
        trackNameLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.96f));
        roleLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.64f));

        for (auto* label : { &titleLabel, &trackNameLabel, &roleLabel, &superColliderLabel, &processorLabel, &audioRegionLabel, &audioFileLabel, &regionGainLabel, &pluginValueLabel, &pluginLaneNameLabel, &pluginLaneStatusLabel, &scInsertStatusLabel, &scInsertMetersLabel, &scInsertWetLabel, &scInsertTrimLabel, &pluginLaneActionsLabel, &volumeLabel, &panLabel, &automationModeLabel, &automationValueLabel, &automationWriteLabel, &pluginParameterLabel, &slotOneLabel, &slotTwoLabel })
            label->setJustificationType(Justification::topLeft);

        scInsertWetSlider.setSliderStyle(Slider::LinearHorizontal);
        scInsertWetSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        scInsertWetSlider.setRange(0.0, 100.0, 0.1);
        scInsertWetSlider.setTextValueSuffix("%");
        scInsertWetSlider.onValueChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto slotIndex = findSuperColliderInsertSlot(*track);
                if (slotIndex >= 0)
                {
                    const auto wetMix = static_cast<float>(scInsertWetSlider.getValue() / 100.0);
                    const auto outputTrimDb = track->inserts[static_cast<size_t>(slotIndex)].outputTrimDb;
                    if (audioEngine.setTrackSuperColliderInsertMix(track->id, slotIndex, wetMix, outputTrimDb)
                        && onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };

        scInsertTrimSlider.setSliderStyle(Slider::LinearHorizontal);
        scInsertTrimSlider.setTextBoxStyle(Slider::TextBoxRight, false, 56, 20);
        scInsertTrimSlider.setRange(-24.0, 24.0, 0.1);
        scInsertTrimSlider.setTextValueSuffix(" dB");
        scInsertTrimSlider.onValueChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto slotIndex = findSuperColliderInsertSlot(*track);
                if (slotIndex >= 0)
                {
                    const auto wetMix = track->inserts[static_cast<size_t>(slotIndex)].wetMix;
                    const auto outputTrimDb = static_cast<float>(scInsertTrimSlider.getValue());
                    if (audioEngine.setTrackSuperColliderInsertMix(track->id, slotIndex, wetMix, outputTrimDb)
                        && onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };

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
        configureAutomationButton(showPluginAutomationButton, "Plugin", AutomationLaneMode::plugin);
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
        pluginParameterBox.onChange = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto selectedIndex = pluginParameterBox.getSelectedId() - 1;
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(track->pluginAutomationLanes.size()))
                {
                    track->selectedPluginAutomationLaneIndex = selectedIndex;
                    track->visibleAutomationLane = AutomationLaneMode::plugin;
                    track->automationExpanded = true;
                    pluginValueSlider.setValue(getSelectedPluginAutomationValue(*track), dontSendNotification);
                    updateAutomationUI(*track);
                    if (onAutomationLayoutChange != nullptr)
                        onAutomationLayoutChange();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };
        movePluginLaneUpButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto index = track->selectedPluginAutomationLaneIndex;
                if (index > 0 && index < static_cast<int>(track->pluginAutomationLanes.size()))
                {
                    std::iter_swap(track->pluginAutomationLanes.begin() + index,
                                   track->pluginAutomationLanes.begin() + (index - 1));
                    track->selectedPluginAutomationLaneIndex = index - 1;
                    refresh();
                    if (onAutomationLayoutChange != nullptr)
                        onAutomationLayoutChange();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };
        movePluginLaneDownButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto index = track->selectedPluginAutomationLaneIndex;
                if (index >= 0 && index + 1 < static_cast<int>(track->pluginAutomationLanes.size()))
                {
                    std::iter_swap(track->pluginAutomationLanes.begin() + index,
                                   track->pluginAutomationLanes.begin() + (index + 1));
                    track->selectedPluginAutomationLaneIndex = index + 1;
                    refresh();
                    if (onAutomationLayoutChange != nullptr)
                        onAutomationLayoutChange();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };
        removePluginLaneButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto index = track->selectedPluginAutomationLaneIndex;
                if (index >= 0 && index < static_cast<int>(track->pluginAutomationLanes.size()))
                {
                    track->pluginAutomationLanes.erase(track->pluginAutomationLanes.begin() + index);
                    if (track->pluginAutomationLanes.empty())
                    {
                        track->selectedPluginAutomationLaneIndex = -1;
                        if (track->visibleAutomationLane == AutomationLaneMode::plugin)
                            track->visibleAutomationLane = AutomationLaneMode::volume;
                    }
                    else
                    {
                        track->selectedPluginAutomationLaneIndex = juce::jmin(index, static_cast<int>(track->pluginAutomationLanes.size()) - 1);
                    }

                    refresh();
                    if (onAutomationLayoutChange != nullptr)
                        onAutomationLayoutChange();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };
        duplicatePluginLaneButton.onClick = [this]
        {
            if (auto* track = session.getSelectedTrack())
            {
                const auto index = track->selectedPluginAutomationLaneIndex;
                if (index >= 0 && index < static_cast<int>(track->pluginAutomationLanes.size()))
                {
                    auto duplicate = track->pluginAutomationLanes[static_cast<size_t>(index)];
                    duplicate.displayName = duplicate.displayName.isNotEmpty()
                        ? duplicate.displayName + " Copy"
                        : duplicate.parameterName + " Copy";
                    track->pluginAutomationLanes.insert(track->pluginAutomationLanes.begin() + index + 1, std::move(duplicate));
                    track->selectedPluginAutomationLaneIndex = index + 1;
                    refresh();
                    if (onAutomationLayoutChange != nullptr)
                        onAutomationLayoutChange();
                    if (onAutomationEdited != nullptr)
                        onAutomationEdited();
                }
            }
        };
        remapPluginLaneButton.onClick = [this]
        {
            auto* track = session.getSelectedTrack();
            auto* pluginLane = track != nullptr ? getSelectedPluginAutomationLane(*track) : nullptr;
            if (track == nullptr || pluginLane == nullptr)
                return;

            const auto parameterChoices = audioEngine.getAutomatableParameters(track->id);
            juce::PopupMenu menu;
            for (int i = 0; i < static_cast<int>(parameterChoices.size()); ++i)
                menu.addItem(i + 1, parameterChoices[static_cast<size_t>(i)].name);

            menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackId = track->id, parameterChoices] (int result)
            {
                if (result <= 0)
                    return;

                auto* targetTrack = session.getSelectedTrack();
                auto* targetLane = targetTrack != nullptr ? getSelectedPluginAutomationLane(*targetTrack) : nullptr;
                if (targetTrack == nullptr || targetTrack->id != trackId || targetLane == nullptr)
                    return;

                const auto choiceIndex = result - 1;
                if (choiceIndex < 0 || choiceIndex >= static_cast<int>(parameterChoices.size()))
                    return;

                const auto& choice = parameterChoices[static_cast<size_t>(choiceIndex)];
                targetLane->slotIndex = choice.slotIndex;
                targetLane->parameterIndex = choice.parameterIndex;
                targetLane->parameterName = choice.name;
                refresh();
                if (onAutomationLayoutChange != nullptr)
                    onAutomationLayoutChange();
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            });
        };
        addPluginAutomationButton.onClick = [this]
        {
            auto* track = session.getSelectedTrack();
            if (track == nullptr)
                return;

            const auto parameterChoices = audioEngine.getAutomatableParameters(track->id);
            juce::PopupMenu menu;

            for (int i = 0; i < static_cast<int>(parameterChoices.size()); ++i)
            {
                const auto& choice = parameterChoices[static_cast<size_t>(i)];
                menu.addItem(i + 1, choice.name);
            }

            menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackId = track->id, parameterChoices] (int result)
            {
                if (result <= 0)
                    return;

                auto* targetTrack = session.getSelectedTrack();
                if (targetTrack == nullptr || targetTrack->id != trackId)
                    return;

                const auto choiceIndex = result - 1;
                if (choiceIndex < 0 || choiceIndex >= static_cast<int>(parameterChoices.size()))
                    return;

                const auto& choice = parameterChoices[static_cast<size_t>(choiceIndex)];
                const auto existingLane = std::find_if(targetTrack->pluginAutomationLanes.begin(),
                                                       targetTrack->pluginAutomationLanes.end(),
                                                       [&choice] (const auto& lane)
                                                       {
                                                           return lane.slotIndex == choice.slotIndex
                                                               && lane.parameterIndex == choice.parameterIndex;
                                                       });

                if (existingLane != targetTrack->pluginAutomationLanes.end())
                    targetTrack->selectedPluginAutomationLaneIndex = static_cast<int>(std::distance(targetTrack->pluginAutomationLanes.begin(), existingLane));
                else
                {
                    PluginAutomationLane lane;
                    lane.slotIndex = choice.slotIndex;
                    lane.parameterIndex = choice.parameterIndex;
                    lane.parameterName = choice.name;
                    lane.displayName = {};
                    targetTrack->pluginAutomationLanes.push_back(std::move(lane));
                    targetTrack->selectedPluginAutomationLaneIndex = static_cast<int>(targetTrack->pluginAutomationLanes.size()) - 1;
                }

                targetTrack->visibleAutomationLane = AutomationLaneMode::plugin;
                targetTrack->automationExpanded = true;
                refresh();
                if (onAutomationLayoutChange != nullptr)
                    onAutomationLayoutChange();
                if (onAutomationEdited != nullptr)
                    onAutomationEdited();
            });
        };

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
            updateInsertMeterBadge(slotOneMeterBadge, *track, 0);
            updateInsertMeterBadge(slotTwoMeterBadge, *track, 1);
            configureInsertSlotRow(*track, 0, loadSlotOneButton, openSlotOneButton, clearSlotOneButton);
            configureInsertSlotRow(*track, 1, loadSlotTwoButton, openSlotTwoButton, clearSlotTwoButton);
            volumeSlider.setValue(track->mixer.volume, dontSendNotification);
            panSlider.setValue(track->mixer.pan, dontSendNotification);
            pluginParameterBox.clear(dontSendNotification);
            for (int i = 0; i < static_cast<int>(track->pluginAutomationLanes.size()); ++i)
                pluginParameterBox.addItem(describePluginAutomationLane(track->pluginAutomationLanes[static_cast<size_t>(i)]), i + 1);

            const auto selectedPluginLaneId = track->selectedPluginAutomationLaneIndex >= 0
                && track->selectedPluginAutomationLaneIndex < static_cast<int>(track->pluginAutomationLanes.size())
                    ? track->selectedPluginAutomationLaneIndex + 1
                    : 0;

            pluginParameterBox.setSelectedId(selectedPluginLaneId, dontSendNotification);
            pluginParameterBox.setEnabled(! track->pluginAutomationLanes.empty());
            addPluginAutomationButton.setEnabled(! audioEngine.getAutomatableParameters(track->id).empty());
            movePluginLaneUpButton.setEnabled(track->selectedPluginAutomationLaneIndex > 0);
            movePluginLaneDownButton.setEnabled(track->selectedPluginAutomationLaneIndex >= 0
                                                && track->selectedPluginAutomationLaneIndex + 1 < static_cast<int>(track->pluginAutomationLanes.size()));
            removePluginLaneButton.setEnabled(track->selectedPluginAutomationLaneIndex >= 0);
            duplicatePluginLaneButton.setEnabled(track->selectedPluginAutomationLaneIndex >= 0);
            if (const auto* pluginLane = getSelectedPluginAutomationLane(*track))
            {
                const auto bindingStatus = getPluginAutomationLaneStatus(*track, *pluginLane);
                pluginValueSlider.setEnabled(bindingStatus.parameterAvailable);
                pluginValueSlider.setValue(getSelectedPluginAutomationValue(*track), dontSendNotification);
                pluginLaneNameEditor.setText(pluginLane->displayName, false);
                if (! bindingStatus.slotAvailable)
                    pluginLaneStatusLabel.setText("Saved target is missing: slot not available.", dontSendNotification);
                else if (! bindingStatus.parameterAvailable)
                    pluginLaneStatusLabel.setText("Saved target is missing: parameter not available. Use Remap.", dontSendNotification);
                else
                    pluginLaneStatusLabel.setText("Bound to " + bindingStatus.resolvedName, dontSendNotification);
                remapPluginLaneButton.setEnabled(! audioEngine.getAutomatableParameters(track->id).empty());
            }
            else
            {
                pluginValueSlider.setEnabled(false);
                pluginValueSlider.setValue(0.5, dontSendNotification);
                pluginLaneNameEditor.setText({}, false);
                pluginLaneStatusLabel.setText({}, dontSendNotification);
                remapPluginLaneButton.setEnabled(false);
            }
            pluginLaneNameEditor.setEnabled(track->selectedPluginAutomationLaneIndex >= 0);
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

            const auto scInsertSlotIndex = findSuperColliderInsertSlot(*track);
            if (scInsertSlotIndex >= 0)
            {
                const auto& insert = track->inserts[static_cast<size_t>(scInsertSlotIndex)];
                const auto hostedMeters = audioEngine.getHostedInsertMeterState(track->id, scInsertSlotIndex);
                const auto hostLive = hostedMeters.has_value() && hostedMeters->active;
                const juce::String statusPrefix = insert.bypassed ? "Bypassed" : (hostLive ? "Host processed" : "Server proxy");
                scInsertMetersLabel.setText("SC insert", dontSendNotification);
                scInsertStatusLabel.setText(statusPrefix
                                                + " / "
                                                + insert.name
                                                + " / In "
                                                + juce::String(hostedMeters.has_value() ? hostedMeters->inputLevel : 0.0f, 2)
                                                + " / Out "
                                                + juce::String(hostedMeters.has_value() ? hostedMeters->outputLevel : 0.0f, 2),
                                            dontSendNotification);
                scInsertWetSlider.setValue(insert.wetMix * 100.0f, dontSendNotification);
                scInsertTrimSlider.setValue(insert.outputTrimDb, dontSendNotification);
                scInsertWetSlider.setEnabled(true);
                scInsertTrimSlider.setEnabled(true);
                scInsertBypassButton.setButtonText(insert.bypassed ? "Enable SC Insert" : "Bypass SC Insert");
                scInsertBypassButton.setEnabled(true);
            }
            else
            {
                scInsertMetersLabel.setText("SC insert", dontSendNotification);
                scInsertStatusLabel.setText("No SuperCollider insert on this track.", dontSendNotification);
                scInsertWetSlider.setValue(100.0, dontSendNotification);
                scInsertTrimSlider.setValue(0.0, dontSendNotification);
                scInsertWetSlider.setEnabled(false);
                scInsertTrimSlider.setEnabled(false);
                scInsertBypassButton.setButtonText("SC Bypass");
                scInsertBypassButton.setEnabled(false);
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
        static constexpr int sectionHeaderHeight = 34;
        auto area = getLocalBounds().reduced(16);
        titleLabel.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);

        const auto remainingHeight = area.getHeight();
        const auto clipBodyHeight = clipSectionExpanded ? 130 : 0;
        const auto clipHeight = jmin(sectionHeaderHeight + clipBodyHeight, remainingHeight);
        clipSectionBounds = area.removeFromTop(clipHeight);
        area.removeFromTop(jmin(12, area.getHeight()));

        const auto trackBodyHeight = trackSectionExpanded ? 286 : 0;
        const auto desiredTrackHeight = sectionHeaderHeight + trackBodyHeight;
        const auto trackHeight = area.getHeight() >= sectionHeaderHeight ? jmin(desiredTrackHeight, area.getHeight()) : 0;
        trackSectionBounds = trackHeight > 0 ? area.removeFromTop(trackHeight) : Rectangle<int>();
        if (trackHeight > 0)
            area.removeFromTop(jmin(12, area.getHeight()));

        const auto channelBodyHeight = channelSectionExpanded ? juce::jmax(0, area.getHeight() - sectionHeaderHeight) : 0;
        const auto channelHeight = area.getHeight() >= sectionHeaderHeight ? sectionHeaderHeight + channelBodyHeight : 0;
        channelSectionBounds = channelHeight > 0 ? area.removeFromTop(channelHeight) : Rectangle<int>();

        if (! clipSectionBounds.isEmpty())
        {
            auto clipHeaderArea = clipSectionBounds.withHeight(sectionHeaderHeight);
            clipSectionToggleButton.setBounds(clipHeaderArea.removeFromRight(28).translated(-10, 4));
        }
        else
            clipSectionToggleButton.setBounds({});

        if (! trackSectionBounds.isEmpty())
        {
            auto trackHeaderArea = trackSectionBounds.withHeight(sectionHeaderHeight);
            trackSectionToggleButton.setBounds(trackHeaderArea.removeFromRight(28).translated(-10, 4));
        }
        else
            trackSectionToggleButton.setBounds({});

        if (! channelSectionBounds.isEmpty())
        {
            auto channelHeaderArea = channelSectionBounds.withHeight(sectionHeaderHeight);
            channelSectionToggleButton.setBounds(channelHeaderArea.removeFromRight(28).translated(-10, 4));
        }
        else
            channelSectionToggleButton.setBounds({});

        auto clipArea = clipSectionBounds.reduced(14, 12);
        if (clipSectionExpanded)
        {
            clipArea.removeFromTop(24);
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
        }
        else
        {
            audioRegionLabel.setBounds({});
            audioFileLabel.setBounds({});
            assignAudioButton.setBounds({});
            clearAudioButton.setBounds({});
            regionGainLabel.setBounds({});
            regionGainSlider.setBounds({});
        }

        if (! trackSectionBounds.isEmpty() && trackSectionExpanded)
        {
            auto trackArea = trackSectionBounds.reduced(14, 16);
            trackArea.removeFromTop(24);
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
            showPluginAutomationButton.setBounds(laneButtons.removeFromLeft(82).reduced(0, 1));
            laneButtons.removeFromLeft(6);
            hideAutomationButton.setBounds(laneButtons.removeFromLeft(68).reduced(0, 1));
            trackArea.removeFromTop(4);
            automationValueLabel.setBounds(trackArea.removeFromTop(16));
            trackArea.removeFromTop(4);
            pluginParameterLabel.setBounds(trackArea.removeFromTop(16));
            auto pluginRow = trackArea.removeFromTop(24);
            pluginParameterBox.setBounds(pluginRow.removeFromLeft(186));
            pluginRow.removeFromLeft(6);
            addPluginAutomationButton.setBounds(pluginRow.removeFromLeft(50).reduced(0, 1));
            trackArea.removeFromTop(4);
            pluginValueLabel.setBounds(trackArea.removeFromTop(16));
            pluginValueSlider.setBounds(trackArea.removeFromTop(24));
            trackArea.removeFromTop(4);
            pluginLaneNameLabel.setBounds(trackArea.removeFromTop(16));
            pluginLaneNameEditor.setBounds(trackArea.removeFromTop(24));
            trackArea.removeFromTop(4);
            pluginLaneStatusLabel.setBounds(trackArea.removeFromTop(18));
            trackArea.removeFromTop(4);
            scInsertMetersLabel.setBounds(trackArea.removeFromTop(16));
            scInsertStatusLabel.setBounds(trackArea.removeFromTop(18));
            trackArea.removeFromTop(4);
            scInsertWetLabel.setBounds(trackArea.removeFromTop(16));
            scInsertWetSlider.setBounds(trackArea.removeFromTop(24));
            trackArea.removeFromTop(4);
            scInsertTrimLabel.setBounds(trackArea.removeFromTop(16));
            scInsertTrimSlider.setBounds(trackArea.removeFromTop(24));
            trackArea.removeFromTop(4);
            scInsertBypassButton.setBounds(trackArea.removeFromTop(24).removeFromLeft(134).reduced(0, 1));
            trackArea.removeFromTop(4);
            pluginLaneActionsLabel.setBounds(trackArea.removeFromTop(16));
            auto laneActionRow = trackArea.removeFromTop(24);
            movePluginLaneUpButton.setBounds(laneActionRow.removeFromLeft(54).reduced(0, 1));
            laneActionRow.removeFromLeft(6);
            movePluginLaneDownButton.setBounds(laneActionRow.removeFromLeft(64).reduced(0, 1));
            laneActionRow.removeFromLeft(6);
            duplicatePluginLaneButton.setBounds(laneActionRow.removeFromLeft(82).reduced(0, 1));
            laneActionRow.removeFromLeft(6);
            remapPluginLaneButton.setBounds(laneActionRow.removeFromLeft(70).reduced(0, 1));
            laneActionRow.removeFromLeft(6);
            removePluginLaneButton.setBounds(laneActionRow.removeFromLeft(78).reduced(0, 1));
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
            pluginParameterLabel.setBounds({});
            pluginParameterBox.setBounds({});
            addPluginAutomationButton.setBounds({});
            pluginValueLabel.setBounds({});
            pluginValueSlider.setBounds({});
            pluginLaneNameLabel.setBounds({});
            pluginLaneNameEditor.setBounds({});
            pluginLaneStatusLabel.setBounds({});
            scInsertMetersLabel.setBounds({});
            scInsertStatusLabel.setBounds({});
            scInsertWetLabel.setBounds({});
            scInsertWetSlider.setBounds({});
            scInsertTrimLabel.setBounds({});
            scInsertTrimSlider.setBounds({});
            scInsertBypassButton.setBounds({});
            pluginLaneActionsLabel.setBounds({});
            movePluginLaneUpButton.setBounds({});
            movePluginLaneDownButton.setBounds({});
            duplicatePluginLaneButton.setBounds({});
            remapPluginLaneButton.setBounds({});
            removePluginLaneButton.setBounds({});
            readAutomationButton.setBounds({});
            touchAutomationButton.setBounds({});
            latchAutomationButton.setBounds({});
            showVolumeAutomationButton.setBounds({});
            showPanAutomationButton.setBounds({});
            showPluginAutomationButton.setBounds({});
            hideAutomationButton.setBounds({});
        }

        if (! channelSectionBounds.isEmpty() && channelSectionExpanded)
        {
            auto channelArea = channelSectionBounds.reduced(14, 16);
            channelArea.removeFromTop(24);
            processorLabel.setBounds(channelArea.removeFromTop(18));
            channelArea.removeFromTop(6);
            auto slotOneHeader = channelArea.removeFromTop(18);
            slotOneMeterBadge.setBounds(slotOneHeader.removeFromRight(58));
            slotOneLabel.setBounds(slotOneHeader);
            auto slotOneButtons = channelArea.removeFromTop(24);
            loadSlotOneButton.setBounds(slotOneButtons.removeFromLeft(82).reduced(0, 1));
            slotOneButtons.removeFromLeft(6);
            openSlotOneButton.setBounds(slotOneButtons.removeFromLeft(58).reduced(0, 1));
            slotOneButtons.removeFromLeft(6);
            clearSlotOneButton.setBounds(slotOneButtons.removeFromLeft(62).reduced(0, 1));
            channelArea.removeFromTop(6);
            auto slotTwoHeader = channelArea.removeFromTop(18);
            slotTwoMeterBadge.setBounds(slotTwoHeader.removeFromRight(58));
            slotTwoLabel.setBounds(slotTwoHeader);
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
            slotOneMeterBadge.setBounds({});
            slotTwoMeterBadge.setBounds({});
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
        if (slot.kind == ProcessorKind::superColliderFx && slot.superCollider.has_value())
        {
            const auto prefix = "Insert " + juce::String(slotIndex + 1);
            return prefix + ": SC " + shortenForSidebar(slot.name, 24);
        }

        if (slot.kind != ProcessorKind::audioUnit || slot.pluginIdentifier.isEmpty())
            return slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi)
                ? "Instrument slot: empty"
                : "Insert " + juce::String(slotIndex + 1) + ": empty";

        const auto prefix = slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi)
            ? "Instrument"
            : "Insert " + juce::String(slotIndex + 1);
        return prefix + ": " + shortenForSidebar(slot.name, 28);
    }

    void updateInsertMeterBadge(InsertRouteMeterComponent& badge, const TrackState& track, int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= static_cast<int>(track.inserts.size()))
        {
            badge.setLevels(0.0f, 0.0f, false);
            return;
        }

        const auto& insert = track.inserts[static_cast<size_t>(slotIndex)];
        if (insert.kind != ProcessorKind::superColliderFx || ! insert.superCollider.has_value())
        {
            badge.setLevels(0.0f, 0.0f, false);
            return;
        }

        const auto meterState = audioEngine.getHostedInsertMeterState(track.id, slotIndex);
        badge.setLevels(meterState.has_value() ? meterState->inputLevel : 0.0f,
                        meterState.has_value() ? meterState->outputLevel : 0.0f,
                        meterState.has_value() && meterState->active && ! insert.bypassed);
    }

    bool hasAudioUnitSlot(const TrackState& track, int slotIndex) const
    {
        return slotIndex >= 0
            && slotIndex < static_cast<int>(track.inserts.size())
            && track.inserts[static_cast<size_t>(slotIndex)].kind == ProcessorKind::audioUnit
            && track.inserts[static_cast<size_t>(slotIndex)].pluginIdentifier.isNotEmpty();
    }

    bool hasSuperColliderSlot(const TrackState& track, int slotIndex) const
    {
        return slotIndex >= 0
            && slotIndex < static_cast<int>(track.inserts.size())
            && track.inserts[static_cast<size_t>(slotIndex)].kind == ProcessorKind::superColliderFx
            && track.inserts[static_cast<size_t>(slotIndex)].superCollider.has_value();
    }

    void configureInsertSlotRow(const TrackState& track,
                                int slotIndex,
                                TextButton& loadButton,
                                TextButton& openButton,
                                TextButton& clearButton) const
    {
        if (hasSuperColliderSlot(track, slotIndex))
        {
            loadButton.setButtonText("SC FX");
            loadButton.setEnabled(false);
            openButton.setButtonText("Live");
            openButton.setEnabled(false);
            clearButton.setButtonText("Reset");
            clearButton.setEnabled(true);
            return;
        }

        const auto choices = audioEngine.getAvailablePluginChoices(track, slotIndex);
        loadButton.setButtonText("Load AU");
        loadButton.setEnabled(! choices.empty());
        openButton.setButtonText("Open");
        openButton.setEnabled(hasAudioUnitSlot(track, slotIndex));
        clearButton.setButtonText("Clear");
        clearButton.setEnabled(hasAudioUnitSlot(track, slotIndex));
    }

    int findSuperColliderInsertSlot(const TrackState& track) const
    {
        for (int i = 0; i < static_cast<int>(track.inserts.size()); ++i)
            if (track.inserts[static_cast<size_t>(i)].kind == ProcessorKind::superColliderFx
                && track.inserts[static_cast<size_t>(i)].superCollider.has_value())
                return i;

        return -1;
    }

    juce::String describePluginAutomationLane(const PluginAutomationLane& lane) const
    {
        auto label = lane.displayName.isNotEmpty()
            ? lane.displayName
            : "S" + juce::String(lane.slotIndex + 1) + " / " + lane.parameterName;
        if (label.isEmpty())
            label = "Plugin lane";
        return label;
    }

    AudioEngine::PluginParameterBindingStatus getPluginAutomationLaneStatus(const TrackState& track, const PluginAutomationLane& lane) const
    {
        return audioEngine.getTrackPluginParameterBindingStatus(track.id, lane.slotIndex, lane.parameterIndex);
    }

    PluginAutomationLane* getSelectedPluginAutomationLane(TrackState& track) const
    {
        if (track.selectedPluginAutomationLaneIndex < 0
            || track.selectedPluginAutomationLaneIndex >= static_cast<int>(track.pluginAutomationLanes.size()))
            return nullptr;

        return &track.pluginAutomationLanes[static_cast<size_t>(track.selectedPluginAutomationLaneIndex)];
    }

    const PluginAutomationLane* getSelectedPluginAutomationLane(const TrackState& track) const
    {
        if (track.selectedPluginAutomationLaneIndex < 0
            || track.selectedPluginAutomationLaneIndex >= static_cast<int>(track.pluginAutomationLanes.size()))
            return nullptr;

        return &track.pluginAutomationLanes[static_cast<size_t>(track.selectedPluginAutomationLaneIndex)];
    }

    float getSelectedPluginAutomationValue(const TrackState& track) const
    {
        if (const auto* pluginLane = getSelectedPluginAutomationLane(track))
            return audioEngine.getTrackPluginParameterValue(track.id, pluginLane->slotIndex, pluginLane->parameterIndex);

        return 0.5f;
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
        showPluginAutomationButton.setToggleState(track.visibleAutomationLane == AutomationLaneMode::plugin && track.automationExpanded, dontSendNotification);
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
        showPluginAutomationButton.setColour(TextButton::buttonColourId,
                                             track.visibleAutomationLane == AutomationLaneMode::plugin && track.automationExpanded
                                                 ? Colour::fromRGB(76, 96, 136)
                                                 : Colour::fromRGB(54, 60, 74));

        if (! track.automationExpanded)
        {
            automationValueLabel.setText("Automation collapsed / mode " + toDisplayString(track.automationWriteMode), dontSendNotification);
            return;
        }

        const auto* pluginLane = getSelectedPluginAutomationLane(track);
        const auto& points = track.visibleAutomationLane == AutomationLaneMode::pan
            ? track.panAutomation
            : (track.visibleAutomationLane == AutomationLaneMode::plugin && pluginLane != nullptr ? pluginLane->points : track.volumeAutomation);
        const auto fallback = track.visibleAutomationLane == AutomationLaneMode::pan ? track.mixer.pan
            : (track.visibleAutomationLane == AutomationLaneMode::plugin ? getSelectedPluginAutomationValue(track) : track.mixer.volume);
        const auto value = interpolateAutomationDisplayValue(points, session.transport.playheadBeat, fallback);
        const auto suffix = track.automationWriteMode == AutomationWriteMode::read
            ? " / read"
            : (track.automationGestureActive || track.automationLatchActive ? " / writing" : " / armed");

        if (track.visibleAutomationLane == AutomationLaneMode::pan)
            automationValueLabel.setText("Current pan: " + String(value, 2) + suffix, dontSendNotification);
        else if (track.visibleAutomationLane == AutomationLaneMode::plugin)
            automationValueLabel.setText(((pluginLane != nullptr) ? describePluginAutomationLane(*pluginLane) : "Plugin parameter")
                                             + ": " + String(value, 2) + suffix,
                                         dontSendNotification);
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

        std::vector<AutomationPoint>* points = nullptr;
        if (laneMode == AutomationLaneMode::pan)
            points = &track.panAutomation;
        else if (laneMode == AutomationLaneMode::plugin)
        {
            if (auto* pluginLane = getSelectedPluginAutomationLane(track))
                points = &pluginLane->points;
        }
        else
            points = &track.volumeAutomation;

        if (points == nullptr)
            return;

        const auto writeBeat = session.transport.playheadBeat;
        const auto clampedValue = laneMode == AutomationLaneMode::pan ? juce::jlimit(-1.0f, 1.0f, value) : juce::jlimit(0.0f, 1.0f, value);

        auto nearby = std::find_if(points->begin(), points->end(), [writeBeat] (const auto& point)
        {
            return std::abs(point.beat - writeBeat) <= 0.15;
        });

        if (nearby != points->end())
        {
            nearby->beat = writeBeat;
            nearby->value = clampedValue;
        }
        else
        {
            AutomationPoint point;
            point.beat = writeBeat;
            point.value = clampedValue;
            points->push_back(point);
            std::sort(points->begin(), points->end(), [] (const auto& left, const auto& right) { return left.beat < right.beat; });
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

    void updateSectionToggleButtons()
    {
        clipSectionToggleButton.setButtonText(clipSectionExpanded ? "v" : ">");
        trackSectionToggleButton.setButtonText(trackSectionExpanded ? "v" : ">");
        channelSectionToggleButton.setButtonText(channelSectionExpanded ? "v" : ">");
        clipSectionToggleButton.setTooltip(clipSectionExpanded ? "Collapse selected clip section" : "Expand selected clip section");
        trackSectionToggleButton.setTooltip(trackSectionExpanded ? "Collapse track section" : "Expand track section");
        channelSectionToggleButton.setTooltip(channelSectionExpanded ? "Collapse channel section" : "Expand channel section");
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
    TextButton clipSectionToggleButton;
    TextButton trackSectionToggleButton;
    TextButton channelSectionToggleButton;
    Label trackNameLabel;
    Label roleLabel;
    Label superColliderLabel;
    Label processorLabel;
    Label audioRegionLabel;
    Label audioFileLabel;
    Label regionGainLabel;
    Label pluginValueLabel;
    Label pluginLaneNameLabel;
    Label pluginLaneStatusLabel;
    Label scInsertStatusLabel;
    Label scInsertMetersLabel;
    Label scInsertWetLabel;
    Label scInsertTrimLabel;
    Label volumeLabel;
    Label panLabel;
    Label automationModeLabel;
    Label automationValueLabel;
    Label automationWriteLabel;
    Label pluginParameterLabel;
    Label pluginLaneActionsLabel;
    ComboBox pluginParameterBox;
    TextButton addPluginAutomationButton;
    Slider regionGainSlider;
    Slider pluginValueSlider;
    juce::TextEditor pluginLaneNameEditor;
    TextButton movePluginLaneUpButton;
    TextButton movePluginLaneDownButton;
    TextButton duplicatePluginLaneButton;
    TextButton remapPluginLaneButton;
    TextButton removePluginLaneButton;
    TextButton scInsertBypassButton;
    Slider scInsertWetSlider;
    Slider scInsertTrimSlider;
    TextButton assignAudioButton;
    TextButton clearAudioButton;
    TextButton showVolumeAutomationButton;
    TextButton showPanAutomationButton;
    TextButton showPluginAutomationButton;
    TextButton hideAutomationButton;
    TextButton readAutomationButton;
    TextButton touchAutomationButton;
    TextButton latchAutomationButton;
    Label slotOneLabel;
    Label slotTwoLabel;
    TextButton loadSlotOneButton;
    TextButton openSlotOneButton;
    TextButton clearSlotOneButton;
    InsertRouteMeterComponent slotOneMeterBadge;
    TextButton loadSlotTwoButton;
    TextButton openSlotTwoButton;
    TextButton clearSlotTwoButton;
    InsertRouteMeterComponent slotTwoMeterBadge;
    Slider volumeSlider;
    Slider panSlider;
    Rectangle<int> trackSectionBounds;
    Rectangle<int> clipSectionBounds;
    Rectangle<int> channelSectionBounds;
    bool clipSectionExpanded { true };
    bool trackSectionExpanded { true };
    bool channelSectionExpanded { true };
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

class MainComponent::LowerPaneSplitterComponent final : public juce::Component
{
public:
    LowerPaneSplitterComponent(std::function<void(int)> onDragToUse,
                               std::function<void()> onDoubleClickToUse)
        : onDrag(std::move(onDragToUse)),
          onDoubleClick(std::move(onDoubleClickToUse))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(Colours::white.withAlpha(isMouseOverOrDragging() ? 0.14f : 0.08f));
        g.fillRect(getLocalBounds());

        auto grip = getLocalBounds().withSizeKeepingCentre(72, 4).toFloat();
        g.setColour(Colours::white.withAlpha(isMouseOverOrDragging() ? 0.30f : 0.22f));
        g.fillRoundedRectangle(grip, 2.0f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartHeight = currentHeight;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (onDrag != nullptr)
            onDrag(dragStartHeight - event.getDistanceFromDragStartY());
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onDoubleClick != nullptr)
            onDoubleClick();
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent&) override { repaint(); }

    void setCurrentHeight(int height)
    {
        currentHeight = height;
    }

private:
    std::function<void(int)> onDrag;
    std::function<void()> onDoubleClick;
    int currentHeight { 318 };
    int dragStartHeight { 318 };
};

class MainComponent::RightSidebarSplitterComponent final : public juce::Component
{
public:
    RightSidebarSplitterComponent(std::function<void(int)> onDragToUse,
                                  std::function<void()> onDoubleClickToUse)
        : onDrag(std::move(onDragToUse)),
          onDoubleClick(std::move(onDoubleClickToUse))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(Colours::white.withAlpha(isMouseOverOrDragging() ? 0.14f : 0.08f));
        g.fillRect(getLocalBounds());

        auto grip = getLocalBounds().withSizeKeepingCentre(4, 72).toFloat();
        g.setColour(Colours::white.withAlpha(isMouseOverOrDragging() ? 0.30f : 0.22f));
        g.fillRoundedRectangle(grip, 2.0f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartWidth = currentWidth;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (onDrag != nullptr)
            onDrag(dragStartWidth - event.getDistanceFromDragStartX());
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onDoubleClick != nullptr)
            onDoubleClick();
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent&) override { repaint(); }

    void setCurrentWidth(int width)
    {
        currentWidth = width;
    }

private:
    std::function<void(int)> onDrag;
    std::function<void()> onDoubleClick;
    int currentWidth { 392 };
    int dragStartWidth { 392 };
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
                                                     [this] { loadProject(); },
                                                     [this] { openAddTrackDialog(); },
                                                     [this] { removeSelectedTrack(); },
                                                     [this]
                                                     {
                                                         juce::PopupMenu menu;
                                                         menu.addItem(1, "Duplicate Track");
                                                         menu.addItem(2, "Duplicate Track With Content");
                                                         menu.showMenuAsync(juce::PopupMenu::Options(),
                                                                            [this] (int selectedItem)
                                                                            {
                                                                                if (selectedItem == 1)
                                                                                    duplicateSelectedTrack(false);
                                                                                else if (selectedItem == 2)
                                                                                    duplicateSelectedTrack(true);
                                                                            });
                                                     });
    arrangeView = std::make_unique<ArrangeViewComponent>(session,
                                                         session.layout.leftSidebarWidth,
                                                         [this] (int trackId) { selectTrack(trackId); },
                                                         [this] (int trackId, int regionIndex) { selectRegion(trackId, regionIndex); },
                                                         [this] { regionEdited(); },
                                                         [this] {
                                                             refreshAllViews(true);
                                                         },
                                                         [this] { markSessionChanged(true); },
                                                         [this] (int width)
                                                         {
                                                             session.layout.leftSidebarWidth = width;
                                                             sessionDirty = true;
                                                             updateWindowState();
                                                         });
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
    audioClipEditor = std::make_unique<AudioClipEditorComponent>(session);
    lowerPaneSplitter = std::make_unique<LowerPaneSplitterComponent>([this] (int proposedHeight)
    {
        if (proposedHeight < 92)
        {
            lowerPaneExpanded = false;
        }
        else
        {
            lowerPaneExpanded = true;
            lowerPaneHeight = juce::jlimit(160, juce::jmax(160, getHeight() - 180), proposedHeight);
        }

        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    },
    [this]
    {
        if (! lowerPaneExpanded || lowerPaneHeight <= 200)
        {
            lowerPaneExpanded = true;
            lowerPaneHeight = 318;
        }
        else
        {
            lowerPaneHeight = 184;
        }

        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    });
    rightSidebarSplitter = std::make_unique<RightSidebarSplitterComponent>([this] (int proposedWidth)
    {
        rightSidebarWidth = juce::jlimit(36, juce::jmax(36, getWidth() - 420), proposedWidth);
        syncLayoutStateToSession(true);
        resized();
    },
    [this]
    {
        rightSidebarWidth = rightSidebarWidth <= 48 ? 392 : 36;
        syncLayoutStateToSession(true);
        resized();
    });
    superColliderOverview = std::make_unique<SuperColliderOverviewComponent>(session, superColliderBridge, [this] { rebuildSynthDefs(); });

    auto configureDockButton = [] (juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, Colour::fromRGB(54, 60, 74));
        button.setColour(juce::TextButton::textColourOffId, Colours::white.withAlpha(0.88f));
    };
    configureDockButton(editorPaneButton);
    configureDockButton(mixerPaneButton);
    configureDockButton(splitPaneButton);
    configureDockButton(lowerPaneToggleButton);
    configureDockButton(editorZoomOutButton);
    configureDockButton(editorZoomInButton);
    configureDockButton(editorPrimaryToolButton);
    configureDockButton(editorSecondaryToolButton);
    editorPaneButton.setButtonText("Editors");
    mixerPaneButton.setButtonText("Mixer");
    splitPaneButton.setButtonText("Split");
    lowerPaneToggleButton.setButtonText("Hide");
    editorZoomOutButton.setButtonText("-");
    editorZoomInButton.setButtonText("+");
    lowerPaneTitleLabel.setColour(juce::Label::textColourId, Colours::white.withAlpha(0.56f));
    lowerPaneTitleLabel.setFont(FontOptions(12.0f, Font::bold));
    lowerPaneTitleLabel.setJustificationType(juce::Justification::centredRight);
    editorPaneButton.onClick = [this]
    {
        lowerPaneMode = LowerPaneMode::editor;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    };
    mixerPaneButton.onClick = [this]
    {
        lowerPaneMode = LowerPaneMode::mixer;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    };
    splitPaneButton.onClick = [this]
    {
        lowerPaneMode = LowerPaneMode::split;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    };
    lowerPaneToggleButton.onClick = [this]
    {
        lowerPaneExpanded = ! lowerPaneExpanded;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
    };
    editorZoomOutButton.onClick = [this]
    {
        const auto* region = session.getSelectedRegion();
        if (region == nullptr)
            return;

        if (region->kind == RegionKind::audio)
            audioClipEditor->zoomOut();
        else
            pianoRoll->zoomOut();

        markSessionChanged(false);
    };
    editorZoomInButton.onClick = [this]
    {
        const auto* region = session.getSelectedRegion();
        if (region == nullptr)
            return;

        if (region->kind == RegionKind::audio)
            audioClipEditor->zoomIn();
        else
            pianoRoll->zoomIn();

        markSessionChanged(false);
    };
    editorPrimaryToolButton.onClick = [this]
    {
        const auto* region = session.getSelectedRegion();
        if (region == nullptr)
            return;

        if (region->kind == RegionKind::audio)
            session.layout.audioEditorTool = 0;
        else
            session.layout.midiEditorTool = 0;

        markSessionChanged(false);
    };
    editorSecondaryToolButton.onClick = [this]
    {
        const auto* region = session.getSelectedRegion();
        if (region == nullptr)
            return;

        if (region->kind == RegionKind::audio)
            session.layout.audioEditorTool = 1;
        else
            session.layout.midiEditorTool = 1;

        markSessionChanged(false);
    };

    addAndMakeVisible(*transport);
    addAndMakeVisible(*arrangeView);
    addAndMakeVisible(*rightSidebarSplitter);
    addAndMakeVisible(*inspector);
    addAndMakeVisible(*lowerPaneSplitter);
    addAndMakeVisible(editorPaneButton);
    addAndMakeVisible(mixerPaneButton);
    addAndMakeVisible(splitPaneButton);
    addAndMakeVisible(lowerPaneToggleButton);
    addAndMakeVisible(lowerPaneTitleLabel);
    addAndMakeVisible(editorZoomOutButton);
    addAndMakeVisible(editorZoomInButton);
    addAndMakeVisible(editorPrimaryToolButton);
    addAndMakeVisible(editorSecondaryToolButton);
    addAndMakeVisible(*pianoRoll);
    addAndMakeVisible(*audioClipEditor);
    addAndMakeVisible(*mixer);
    addAndMakeVisible(*superColliderOverview);

    restoreLayoutStateFromSession();
    restoreSelectionSpecificLayoutState();
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

void MainComponent::restoreLayoutStateFromSession()
{
    rightSidebarWidth = session.layout.rightSidebarWidth;
    lowerPaneHeight = session.layout.lowerPaneHeight;
    lowerPaneExpanded = session.layout.lowerPaneExpanded;
    lowerPaneMode = storedValueToLowerPaneMode(session.layout.lowerPaneModeValue);

    if (arrangeView != nullptr)
        arrangeView->setHeaderWidth(session.layout.leftSidebarWidth);
}

void MainComponent::syncLayoutStateToSession(bool markDirty)
{
    session.layout.rightSidebarWidth = rightSidebarWidth;
    session.layout.lowerPaneHeight = lowerPaneHeight;
    session.layout.lowerPaneExpanded = lowerPaneExpanded;
    session.layout.lowerPaneModeValue = lowerPaneModeToStoredValue(lowerPaneMode);

    if (markDirty)
    {
        sessionDirty = true;
        updateWindowState();
    }
}

void MainComponent::syncSelectionSpecificLayoutState(bool markDirty)
{
    const auto* region = session.getSelectedRegion();
    if (region == nullptr)
        return;

    const auto isMidiSelection = region->kind == RegionKind::midi || region->kind == RegionKind::generated;
    if (isMidiSelection)
    {
        session.layout.midiSelectionExpanded = lowerPaneExpanded;
        session.layout.midiSelectionModeValue = lowerPaneModeToStoredValue(lowerPaneMode);
    }
    else if (region->kind == RegionKind::audio)
    {
        session.layout.audioSelectionExpanded = lowerPaneExpanded;
        session.layout.audioSelectionModeValue = lowerPaneModeToStoredValue(lowerPaneMode);
    }

    if (markDirty)
    {
        sessionDirty = true;
        updateWindowState();
    }
}

void MainComponent::restoreSelectionSpecificLayoutState()
{
    const auto* region = session.getSelectedRegion();
    if (region == nullptr)
        return;

    const auto isMidiSelection = region->kind == RegionKind::midi || region->kind == RegionKind::generated;
    if (isMidiSelection)
    {
        lowerPaneExpanded = session.layout.midiSelectionExpanded;
        lowerPaneMode = storedValueToLowerPaneMode(session.layout.midiSelectionModeValue);
    }
    else if (region->kind == RegionKind::audio)
    {
        lowerPaneExpanded = session.layout.audioSelectionExpanded;
        lowerPaneMode = storedValueToLowerPaneMode(session.layout.audioSelectionModeValue);
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    transport->setBounds(area.removeFromTop(76));

    const auto* selectedRegion = session.getSelectedRegion();
    const auto selectedRegionIsMidi = selectedRegion != nullptr
        && (selectedRegion->kind == RegionKind::midi || selectedRegion->kind == RegionKind::generated);
    const auto selectedRegionIsAudio = selectedRegion != nullptr && selectedRegion->kind == RegionKind::audio;
    const auto showEditorDock = lowerPaneExpanded
        && (lowerPaneMode == LowerPaneMode::editor || lowerPaneMode == LowerPaneMode::split);
    const auto showMixerDock = lowerPaneExpanded
        && (lowerPaneMode == LowerPaneMode::mixer || lowerPaneMode == LowerPaneMode::split);
    const auto showMidiEditor = showEditorDock && selectedRegionIsMidi;
    const auto showAudioEditor = showEditorDock && selectedRegionIsAudio;
    const auto showEditorTools = showEditorDock && selectedRegion != nullptr;

    const auto expandedDockHeight = juce::jlimit(160, juce::jmax(160, getHeight() - 180), lowerPaneHeight);
    auto lowerDock = area.removeFromBottom(lowerPaneExpanded ? expandedDockHeight : 44);
    if (lowerPaneExpanded)
    {
        lowerPaneSplitter->setVisible(true);
        lowerPaneSplitter->setBounds(lowerDock.getX(), lowerDock.getY() - 8, lowerDock.getWidth(), 8);
        lowerPaneSplitter->setCurrentHeight(lowerPaneHeight);
    }
    else
    {
        lowerPaneSplitter->setVisible(false);
        lowerPaneSplitter->setBounds({});
    }
    auto dockHeader = lowerDock.removeFromTop(44).reduced(12, 8);
    editorPaneButton.setBounds(dockHeader.removeFromLeft(94).reduced(0, 1));
    dockHeader.removeFromLeft(8);
    mixerPaneButton.setBounds(dockHeader.removeFromLeft(84).reduced(0, 1));
    dockHeader.removeFromLeft(8);
    splitPaneButton.setBounds(dockHeader.removeFromLeft(78).reduced(0, 1));
    lowerPaneToggleButton.setBounds(dockHeader.removeFromRight(72).reduced(0, 1));
    dockHeader.removeFromRight(8);
    if (showEditorTools)
    {
        editorSecondaryToolButton.setVisible(true);
        editorPrimaryToolButton.setVisible(true);
        editorZoomInButton.setVisible(true);
        editorZoomOutButton.setVisible(true);

        editorSecondaryToolButton.setBounds(dockHeader.removeFromRight(86).reduced(0, 1));
        dockHeader.removeFromRight(6);
        editorPrimaryToolButton.setBounds(dockHeader.removeFromRight(86).reduced(0, 1));
        dockHeader.removeFromRight(10);
        editorZoomInButton.setBounds(dockHeader.removeFromRight(32).reduced(0, 1));
        dockHeader.removeFromRight(4);
        editorZoomOutButton.setBounds(dockHeader.removeFromRight(32).reduced(0, 1));
        dockHeader.removeFromRight(10);
    }
    else
    {
        editorSecondaryToolButton.setVisible(false);
        editorPrimaryToolButton.setVisible(false);
        editorZoomInButton.setVisible(false);
        editorZoomOutButton.setVisible(false);
        editorSecondaryToolButton.setBounds({});
        editorPrimaryToolButton.setBounds({});
        editorZoomInButton.setBounds({});
        editorZoomOutButton.setBounds({});
    }

    lowerPaneTitleLabel.setBounds(dockHeader);

    const auto visibleRightSidebarWidth = juce::jlimit(36, juce::jmax(36, getWidth() - 420), rightSidebarWidth);
    auto rightSidebar = area.removeFromRight(visibleRightSidebarWidth);
    rightSidebarSplitter->setBounds(rightSidebar.getX() - 8, rightSidebar.getY(), 8, rightSidebar.getHeight());
    rightSidebarSplitter->setCurrentWidth(rightSidebarWidth);

    const auto rightSidebarCollapsed = visibleRightSidebarWidth <= 48;
    if (rightSidebarCollapsed)
    {
        superColliderOverview->setVisible(false);
        inspector->setVisible(false);
        superColliderOverview->setBounds({});
        inspector->setBounds({});
    }
    else
    {
        superColliderOverview->setVisible(true);
        inspector->setVisible(true);
        superColliderOverview->setBounds(rightSidebar.removeFromTop(198).reduced(12, 10));
        rightSidebar.removeFromTop(6);
        inspector->setBounds(rightSidebar.reduced(12, 8));
    }
    arrangeView->setBounds(area);

    auto editorBounds = juce::Rectangle<int>();
    auto mixerBounds = juce::Rectangle<int>();
    if (showEditorDock && showMixerDock)
    {
        auto splitArea = lowerDock.reduced(6, 4);
        editorBounds = splitArea.removeFromTop(juce::jmax(120, (splitArea.getHeight() * 3) / 5)).reduced(4, 2);
        splitArea.removeFromTop(4);
        mixerBounds = splitArea.reduced(0, 2);
    }
    else if (showEditorDock)
    {
        editorBounds = lowerDock.reduced(10, 6);
    }
    else if (showMixerDock)
    {
        mixerBounds = lowerDock.reduced(4, 0);
    }

    if (showMidiEditor)
    {
        pianoRoll->setVisible(true);
        pianoRoll->setBounds(editorBounds);
        audioClipEditor->setVisible(false);
        audioClipEditor->setBounds({});
    }
    else if (showAudioEditor || showEditorDock)
    {
        audioClipEditor->setVisible(true);
        audioClipEditor->setBounds(editorBounds);
        pianoRoll->setVisible(false);
        pianoRoll->setBounds({});
    }
    else
    {
        pianoRoll->setVisible(false);
        audioClipEditor->setVisible(false);
        pianoRoll->setBounds({});
        audioClipEditor->setBounds({});
    }

    mixer->setVisible(showMixerDock);
    mixer->setBounds(showMixerDock ? mixerBounds : juce::Rectangle<int>());
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

    if (key == juce::KeyPress('e', juce::ModifierKeys(), 0))
    {
        lowerPaneMode = LowerPaneMode::editor;
        lowerPaneExpanded = ! lowerPaneExpanded ? true : lowerPaneExpanded;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
        return true;
    }

    if (key == juce::KeyPress('x', juce::ModifierKeys(), 0))
    {
        lowerPaneMode = LowerPaneMode::mixer;
        lowerPaneExpanded = ! lowerPaneExpanded ? true : lowerPaneExpanded;
        syncLayoutStateToSession(true);
        syncSelectionSpecificLayoutState(true);
        updateWindowState();
        resized();
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
    restoreSelectionSpecificLayoutState();
    syncLayoutStateToSession(false);
    refreshAllViews(true);
    resized();
    updateWindowState();
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

    if (audioClipEditor != nullptr)
        audioClipEditor->refresh();

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
        restoreLayoutStateFromSession();
        restoreSelectionSpecificLayoutState();
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
    audioEngine.syncPluginStatesToSession();

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
                                                               juce::File(currentProjectPath.isNotEmpty() ? currentProjectPath : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("cigoL.cigol")),
                                                               "*.cigol");
    activeProjectChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto selected = chooser.getResult();
                                          activeProjectChooser.reset();
                                          if (selected == juce::File())
                                              return;

                                          auto target = selected;
                                          if (! target.hasFileExtension(".cigol"))
                                              target = target.withFileExtension(".cigol");

                                          const auto result = saveSessionToFile(session, target);
                                          if (result.wasOk())
                                          {
                                              currentProjectPath = target.getFullPathName();
                                              sessionDirty = false;
                                              updateWindowState();
                                          }
                                      });
}

void MainComponent::openAddTrackDialog()
{
    auto addCountItems = [] (juce::PopupMenu& menu, int baseId)
    {
        menu.addItem(baseId + 1, "1 Track");
        menu.addItem(baseId + 2, "2 Tracks");
        menu.addItem(baseId + 4, "4 Tracks");
        menu.addItem(baseId + 8, "8 Tracks");
    };

    juce::PopupMenu audioStereoMenu;
    juce::PopupMenu audioMonoMenu;
    juce::PopupMenu midiMenu;
    juce::PopupMenu instrumentMenu;
    juce::PopupMenu superColliderMenu;
    addCountItems(audioStereoMenu, 100);
    addCountItems(audioMonoMenu, 200);
    addCountItems(midiMenu, 300);
    addCountItems(instrumentMenu, 400);
    addCountItems(superColliderMenu, 500);

    juce::PopupMenu audioMenu;
    audioMenu.addSubMenu("Stereo", audioStereoMenu);
    audioMenu.addSubMenu("Mono", audioMonoMenu);

    juce::PopupMenu menu;
    menu.addSubMenu("Audio Track", audioMenu);
    menu.addSubMenu("MIDI Track", midiMenu);
    menu.addSubMenu("Instrument Track", instrumentMenu);
    menu.addSubMenu("SuperCollider Render Track", superColliderMenu);

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [this] (int selectedItem)
                       {
                           if (selectedItem <= 0)
                               return;

                           const auto group = selectedItem / 100;
                           const auto count = juce::jlimit(1, 64, selectedItem % 100);

                           switch (group)
                           {
                               case 1: addTracks(TrackKind::audio, TrackChannelMode::stereo, count); break;
                               case 2: addTracks(TrackKind::audio, TrackChannelMode::mono, count); break;
                               case 3: addTracks(TrackKind::midi, TrackChannelMode::stereo, count); break;
                               case 4: addTracks(TrackKind::instrument, TrackChannelMode::stereo, count); break;
                               case 5: addTracks(TrackKind::superColliderRender, TrackChannelMode::stereo, count); break;
                               default: break;
                           }
                       });
}

void MainComponent::addTracks(TrackKind kind, TrackChannelMode channelMode, int count)
{
    const auto safeCount = juce::jlimit(1, 64, count);
    auto nextId = std::accumulate(session.tracks.begin(), session.tracks.end(), 1, [] (int current, const auto& track)
    {
        return juce::jmax(current, track.id + 1);
    });

    auto ordinal = 1 + static_cast<int>(std::count_if(session.tracks.begin(), session.tracks.end(), [kind] (const auto& track)
    {
        return track.kind == kind;
    }));

    for (auto& track : session.tracks)
        track.selected = false;

    int lastTrackId = -1;
    for (int i = 0; i < safeCount; ++i)
    {
        auto track = makeDefaultTrack(kind, channelMode, nextId, ordinal);
        track.selected = false;
        session.tracks.push_back(std::move(track));
        lastTrackId = nextId;
        ++nextId;
        ++ordinal;
    }

    if (lastTrackId > 0)
    {
        session.selectTrack(lastTrackId);
        session.selectRegion(lastTrackId, 0);
    }

    audioEngine.reloadSessionState();
    superColliderBridge.refreshEnvironment(session);
    refreshAllViews(true);
    markSessionChanged(true, true);
    resized();
    updateWindowState();
}

void MainComponent::duplicateSelectedTrack(bool includeContent)
{
    auto* selectedTrack = session.getSelectedTrack();
    if (selectedTrack == nullptr)
        return;

    const auto sourceTrackId = selectedTrack->id;
    const auto nextId = std::accumulate(session.tracks.begin(), session.tracks.end(), 1, [] (int current, const auto& track)
    {
        return juce::jmax(current, track.id + 1);
    });

    auto duplicate = *selectedTrack;
    duplicate.id = nextId;
    duplicate.selected = false;
    duplicate.name = selectedTrack->name + " Copy";
    duplicate.mixer.meterLevel = 0.0f;
    duplicate.automationGestureActive = false;
    duplicate.automationLatchActive = false;
    duplicate.automationWriteTarget = AutomationLaneMode::none;

    if (! includeContent)
    {
        duplicate.volumeAutomation.clear();
        duplicate.panAutomation.clear();
        for (auto& lane : duplicate.pluginAutomationLanes)
            lane.points.clear();
        duplicate.regions.clear();

        if (duplicate.kind == TrackKind::audio)
            duplicate.regions.push_back({ "Audio Clip", duplicate.colour, RegionKind::audio, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
        else if (duplicate.kind == TrackKind::midi)
            duplicate.regions.push_back({ "MIDI Clip", duplicate.colour, RegionKind::midi, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
        else if (duplicate.kind == TrackKind::instrument)
            duplicate.regions.push_back({ "Instrument Clip", duplicate.colour, RegionKind::midi, 1.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
        else if (duplicate.kind == TrackKind::superColliderRender)
            duplicate.regions.push_back({ "Generated", duplicate.colour, RegionKind::generated, 1.0, 8.0, {}, 0.0, 0.0, 0.0, 1.0f, {} });
    }

    for (auto& track : session.tracks)
        track.selected = false;

    auto insertPosition = std::find_if(session.tracks.begin(), session.tracks.end(), [sourceTrackId] (const auto& track)
    {
        return track.id == sourceTrackId;
    });

    if (insertPosition == session.tracks.end())
        return;

    duplicate.selected = true;
    session.tracks.insert(insertPosition + 1, std::move(duplicate));
    session.selectTrack(nextId);
    session.selectRegion(nextId, 0);
    audioEngine.reloadSessionState();
    superColliderBridge.refreshEnvironment(session);
    refreshAllViews(true);
    markSessionChanged(true, true);
    resized();
    updateWindowState();
}

void MainComponent::removeSelectedTrack()
{
    if (session.tracks.size() <= 1)
        return;

    const auto selectedTrackId = session.selectedTrackId;
    closeAllAudioUnitEditorWindows();

    const auto removeIt = std::find_if(session.tracks.begin(), session.tracks.end(), [selectedTrackId] (const auto& track)
    {
        return track.id == selectedTrackId;
    });

    if (removeIt == session.tracks.end())
        return;

    const auto removedIndex = static_cast<int>(std::distance(session.tracks.begin(), removeIt));
    session.tracks.erase(removeIt);

    const auto replacementIndex = juce::jlimit(0, static_cast<int>(session.tracks.size()) - 1, removedIndex);
    const auto replacementTrackId = session.tracks[static_cast<size_t>(replacementIndex)].id;
    session.selectTrack(replacementTrackId);
    if (! session.tracks[static_cast<size_t>(replacementIndex)].regions.empty())
        session.selectRegion(replacementTrackId, 0);
    else
    {
        session.selectedRegionTrackId = replacementTrackId;
        session.selectedRegionIndex = -1;
    }

    audioEngine.reloadSessionState();
    superColliderBridge.refreshEnvironment(session);
    restoreSelectionSpecificLayoutState();
    refreshAllViews(true);
    markSessionChanged(true, true);
    resized();
    updateWindowState();
}

void MainComponent::loadProject()
{
    activeProjectChooser = std::make_unique<juce::FileChooser>("Load project",
                                                               juce::File(currentProjectPath),
                                                               "*.cigol");
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
                                          restoreLayoutStateFromSession();
                                          restoreSelectionSpecificLayoutState();
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
        : juce::String("Untitled cigoL Project");
    transport->setProjectStatus(projectName,
                                sessionDirty || undoSnapshotPending,
                                undoSnapshots.size() > 1,
                                ! redoSnapshots.empty(),
                                session.tracks.size() > 1,
                                session.getSelectedTrack() != nullptr);

    auto tintDockButton = [] (juce::TextButton& button, bool active)
    {
        button.setColour(juce::TextButton::buttonColourId,
                         active ? Colour::fromRGB(84, 98, 128) : Colour::fromRGB(54, 60, 74));
    };
    tintDockButton(editorPaneButton, lowerPaneMode == LowerPaneMode::editor);
    tintDockButton(mixerPaneButton, lowerPaneMode == LowerPaneMode::mixer);
    tintDockButton(splitPaneButton, lowerPaneMode == LowerPaneMode::split);
    lowerPaneToggleButton.setButtonText(lowerPaneExpanded ? "Hide" : "Show");

    const auto* region = session.getSelectedRegion();
    juce::String editorLabel = "Editors hidden";
    if (lowerPaneExpanded)
    {
        if (lowerPaneMode == LowerPaneMode::mixer)
            editorLabel = "Mixer";
        else if (lowerPaneMode == LowerPaneMode::split)
            editorLabel = "Editors + Mixer";
        else if (region == nullptr)
            editorLabel = "Editor / no region selected";
        else if (region->kind == RegionKind::audio)
            editorLabel = "Audio File Editor / " + region->name;
        else
            editorLabel = "Piano Roll / " + region->name;
    }

    lowerPaneTitleLabel.setText(editorLabel, juce::dontSendNotification);

    const auto showEditorTools = lowerPaneExpanded
        && region != nullptr
        && (lowerPaneMode == LowerPaneMode::editor || lowerPaneMode == LowerPaneMode::split);

    editorZoomOutButton.setVisible(showEditorTools);
    editorZoomInButton.setVisible(showEditorTools);
    editorPrimaryToolButton.setVisible(showEditorTools);
    editorSecondaryToolButton.setVisible(showEditorTools);

    if (! showEditorTools)
        return;

    const auto isAudioEditor = region->kind == RegionKind::audio;
    const auto activeTool = isAudioEditor ? session.layout.audioEditorTool : session.layout.midiEditorTool;
    editorPrimaryToolButton.setButtonText(isAudioEditor ? "Pointer" : "Select");
    editorSecondaryToolButton.setButtonText(isAudioEditor ? "Zoom" : "Draw");
    tintDockButton(editorPrimaryToolButton, activeTool == 0);
    tintDockButton(editorSecondaryToolButton, activeTool == 1);
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

                                       auto applyImportedFile = [this, selectedTrackId, selectedRegionIndex, result] (bool useNaturalDuration)
                                       {
                                           auto it = std::find_if(session.tracks.begin(), session.tracks.end(), [selectedTrackId] (const auto& candidate) {
                                               return candidate.id == selectedTrackId;
                                           });

                                           if (it == session.tracks.end())
                                               return;

                                           if (selectedRegionIndex < 0
                                               || selectedRegionIndex >= static_cast<int>(it->regions.size()))
                                               return;

                                           auto* selectedRegion = &it->regions[static_cast<size_t>(selectedRegionIndex)];
                                           selectedRegion->sourceFilePath = result.getFullPathName();
                                           selectedRegion->sourceOffsetSeconds = 0.0;

                                           if (useNaturalDuration)
                                           {
                                               if (const auto durationSeconds = readAudioFileDurationSeconds(result); durationSeconds.has_value())
                                               {
                                                   const auto beats = juce::jmax(0.25, (*durationSeconds * juce::jmax(1.0, session.transport.bpm)) / 60.0);
                                                   selectedRegion->lengthInBeats = beats;
                                               }
                                           }

                                           const auto maximumFade = juce::jmax(0.0, selectedRegion->lengthInBeats - 0.25);
                                           selectedRegion->fadeInBeats = juce::jlimit(0.0, maximumFade, selectedRegion->fadeInBeats);
                                           selectedRegion->fadeOutBeats = juce::jlimit(0.0, maximumFade, selectedRegion->fadeOutBeats);
                                           markSessionChanged(true, true);
                                       };

                                       if (const auto durationSeconds = readAudioFileDurationSeconds(result); durationSeconds.has_value())
                                       {
                                           juce::PopupMenu menu;
                                           menu.addItem(1, "Keep current clip length");
                                           menu.addItem(2, "Import at natural duration");

                                           menu.showMenuAsync(juce::PopupMenu::Options(),
                                                              [applyImportedFile] (int selectedItem)
                                                              {
                                                                  if (selectedItem == 1)
                                                                      applyImportedFile(false);
                                                                  else if (selectedItem == 2)
                                                                      applyImportedFile(true);
                                                              });
                                           return;
                                       }

                                       applyImportedFile(false);
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

    if (slotIndex >= 0
        && slotIndex < static_cast<int>(track->inserts.size())
        && track->inserts[static_cast<size_t>(slotIndex)].kind == ProcessorKind::superColliderFx
        && track->inserts[static_cast<size_t>(slotIndex)].superCollider.has_value())
    {
        audioEngine.setTrackSlotBypassed(track->id, slotIndex, false);
        audioEngine.setTrackSuperColliderInsertMix(track->id, slotIndex, 1.0f, 0.0f);
        markSessionChanged(true, true);
        return;
    }

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
} // namespace cigol

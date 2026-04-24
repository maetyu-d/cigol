#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace cigol
{
enum class TrackKind
{
    audio,
    midi,
    instrument,
    superColliderRender
};

enum class TrackChannelMode
{
    mono,
    stereo
};

enum class RegionKind
{
    audio,
    midi,
    generated
};

enum class ProcessorKind
{
    audioUnit,
    superColliderFx
};

enum class MidiGeneratorKind
{
    none,
    superCollider
};

struct Region
{
    struct MidiNote
    {
        int pitch { 60 };
        double startBeat { 1.0 };
        double lengthInBeats { 1.0 };
        uint8_t velocity { 100 };
        bool selected { false };
    };

    juce::String name;
    juce::Colour colour;
    RegionKind kind { RegionKind::audio };
    double startBeat {};
    double lengthInBeats {};
    juce::String sourceFilePath;
    double sourceOffsetSeconds { 0.0 };
    double fadeInBeats { 0.0 };
    double fadeOutBeats { 0.0 };
    float gain { 1.0f };
    std::vector<MidiNote> midiNotes;
};

struct MixerState
{
    float volume { 0.78f };
    float pan { 0.0f };
    float meterLevel { 0.0f };
};

struct AutomationPoint
{
    double beat { 1.0 };
    float value { 1.0f };
    enum class SegmentShape
    {
        linear,
        easeIn,
        easeOut,
        step
    };

    SegmentShape shapeToNext { SegmentShape::linear };
};

enum class AutomationLaneMode
{
    none,
    volume,
    pan,
    plugin
};

enum class AutomationWriteMode
{
    read,
    touch,
    latch
};

struct SuperColliderScriptState
{
    juce::String scriptName;
    juce::String code;
    juce::String synthDefName;
    juce::String entryNode;
    juce::String busRouting;
    juce::String statusLine;
    bool enabled { true };
    bool rendersOfflineStem { false };
};

struct TrackProcessorState
{
    ProcessorKind kind { ProcessorKind::audioUnit };
    juce::String name;
    juce::String pluginIdentifier;
    juce::String pluginStateBase64;
    bool bypassed { false };
    float wetMix { 1.0f };
    float outputTrimDb { 0.0f };
    std::optional<SuperColliderScriptState> superCollider;
};

struct PluginAutomationLane
{
    int slotIndex { -1 };
    int parameterIndex { -1 };
    juce::String parameterName;
    juce::String displayName;
    std::vector<AutomationPoint> points;
};

struct MidiGeneratorState
{
    MidiGeneratorKind kind { MidiGeneratorKind::none };
    juce::String name;
    bool enabled { false };
    bool followsTransport { true };
    std::optional<SuperColliderScriptState> superCollider;
};

struct TrackState
{
    int id {};
    juce::String name;
    juce::String role;
    TrackKind kind { TrackKind::audio };
    TrackChannelMode channelMode { TrackChannelMode::stereo };
    juce::Colour colour;
    bool armed { false };
    bool muted { false };
    bool solo { false };
    bool selected { false };
    MixerState mixer;
    AutomationLaneMode visibleAutomationLane { AutomationLaneMode::volume };
    bool automationExpanded { false };
    AutomationWriteMode automationWriteMode { AutomationWriteMode::read };
    AutomationLaneMode automationWriteTarget { AutomationLaneMode::none };
    bool automationGestureActive { false };
    bool automationLatchActive { false };
    std::vector<AutomationPoint> volumeAutomation;
    std::vector<AutomationPoint> panAutomation;
    std::vector<PluginAutomationLane> pluginAutomationLanes;
    int selectedPluginAutomationLaneIndex { -1 };
    std::vector<Region> regions;
    std::vector<TrackProcessorState> inserts;
    MidiGeneratorState midiGenerator;
    std::optional<SuperColliderScriptState> renderScript;
};

struct TransportState
{
    bool playing { false };
    bool recording { false };
    double bpm { 120.0 };
    double playheadBeat { 1.0 };
    double visibleBeats { 32.0 };
};

struct RoutingState
{
    bool superColliderServerConnected { true };
    juce::String renderBusName { "SC Render Bus A" };
    juce::String fxBusName { "SC FX Bus A" };
    juce::String midiBusName { "SC MIDI Clock" };
};

struct EditorLayoutState
{
    int leftSidebarWidth { 240 };
    int rightSidebarWidth { 392 };
    int lowerPaneHeight { 318 };
    bool lowerPaneExpanded { true };
    int lowerPaneModeValue { 0 };
    bool audioSelectionExpanded { true };
    int audioSelectionModeValue { 0 };
    bool midiSelectionExpanded { true };
    int midiSelectionModeValue { 0 };
    float audioEditorZoom { 1.0f };
    float midiEditorZoom { 1.0f };
    int audioEditorTool { 0 };
    int midiEditorTool { 0 };
};

struct SessionState
{
    TransportState transport;
    RoutingState routing;
    EditorLayoutState layout;
    std::vector<TrackState> tracks;
    int selectedTrackId { 1 };
    int selectedRegionTrackId { 1 };
    int selectedRegionIndex { -1 };

    TrackState* getSelectedTrack();
    const TrackState* getSelectedTrack() const;
    Region* getSelectedRegion();
    const Region* getSelectedRegion() const;
    void selectTrack(int trackId);
    void selectRegion(int trackId, int regionIndex);
};

SessionState createDemoSession();
juce::String serialiseSessionToJson(const SessionState& session);
juce::Result deserialiseSessionFromJson(SessionState& session, const juce::String& jsonText);
juce::Result saveSessionToFile(const SessionState& session, const juce::File& file);
juce::Result loadSessionFromFile(SessionState& session, const juce::File& file);

juce::String toDisplayString(TrackKind kind);
juce::String toDisplayString(TrackChannelMode mode);
juce::String toDisplayString(ProcessorKind kind);
juce::String toDisplayString(MidiGeneratorKind kind);
juce::String toDisplayString(AutomationLaneMode mode);
juce::String toDisplayString(AutomationWriteMode mode);
juce::String toDisplayString(AutomationPoint::SegmentShape shape);
} // namespace cigol

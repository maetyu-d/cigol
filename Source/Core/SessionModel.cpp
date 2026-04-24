#include "SessionModel.h"

#include <algorithm>

namespace cigol
{
namespace
{
using juce::DynamicObject;
using juce::File;
using juce::JSON;
using juce::String;
using juce::var;

std::vector<Region::MidiNote> makeMidiPhrase(std::initializer_list<Region::MidiNote> notes)
{
    return std::vector<Region::MidiNote>(notes);
}

template <typename Enum>
int enumToInt(Enum value)
{
    return static_cast<int>(value);
}

template <typename Enum>
Enum intToEnum(const var& value, Enum fallback)
{
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble() && ! value.isBool())
        return fallback;

    return static_cast<Enum>(static_cast<int>(value));
}

var colourToVar(juce::Colour colour)
{
    return static_cast<int>(colour.getARGB());
}

juce::Colour colourFromVar(const var& value, juce::Colour fallback = juce::Colours::darkgrey)
{
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
        return fallback;

    return juce::Colour(static_cast<juce::uint32>(static_cast<int>(value)));
}

var midiNoteToVar(const Region::MidiNote& note)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("pitch", note.pitch);
    object->setProperty("startBeat", note.startBeat);
    object->setProperty("lengthInBeats", note.lengthInBeats);
    object->setProperty("velocity", static_cast<int>(note.velocity));
    return var(object.release());
}

Region::MidiNote midiNoteFromVar(const var& value)
{
    Region::MidiNote note;
    if (auto* object = value.getDynamicObject())
    {
        note.pitch = static_cast<int>(object->getProperty("pitch"));
        note.startBeat = static_cast<double>(object->getProperty("startBeat"));
        note.lengthInBeats = static_cast<double>(object->getProperty("lengthInBeats"));
        note.velocity = static_cast<juce::uint8>(static_cast<int>(object->getProperty("velocity")));
    }

    return note;
}

var automationPointToVar(const AutomationPoint& point)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("beat", point.beat);
    object->setProperty("value", point.value);
    object->setProperty("shapeToNext", enumToInt(point.shapeToNext));
    return var(object.release());
}

AutomationPoint automationPointFromVar(const var& value)
{
    AutomationPoint point;
    if (auto* object = value.getDynamicObject())
    {
        point.beat = static_cast<double>(object->getProperty("beat"));
        point.value = static_cast<float>(static_cast<double>(object->getProperty("value")));
        point.shapeToNext = intToEnum(object->getProperty("shapeToNext"), AutomationPoint::SegmentShape::linear);
    }

    return point;
}

var superColliderScriptToVar(const SuperColliderScriptState& script)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("scriptName", script.scriptName);
    object->setProperty("code", script.code);
    object->setProperty("synthDefName", script.synthDefName);
    object->setProperty("entryNode", script.entryNode);
    object->setProperty("busRouting", script.busRouting);
    object->setProperty("statusLine", script.statusLine);
    object->setProperty("enabled", script.enabled);
    object->setProperty("rendersOfflineStem", script.rendersOfflineStem);
    return var(object.release());
}

SuperColliderScriptState superColliderScriptFromVar(const var& value)
{
    SuperColliderScriptState script;
    if (auto* object = value.getDynamicObject())
    {
        script.scriptName = object->getProperty("scriptName").toString();
        script.code = object->getProperty("code").toString();
        script.synthDefName = object->getProperty("synthDefName").toString();
        script.entryNode = object->getProperty("entryNode").toString();
        script.busRouting = object->getProperty("busRouting").toString();
        script.statusLine = object->getProperty("statusLine").toString();
        script.enabled = static_cast<bool>(object->getProperty("enabled"));
        script.rendersOfflineStem = static_cast<bool>(object->getProperty("rendersOfflineStem"));
    }

    return script;
}

var processorToVar(const TrackProcessorState& processor)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("kind", enumToInt(processor.kind));
    object->setProperty("name", processor.name);
    object->setProperty("pluginIdentifier", processor.pluginIdentifier);
    object->setProperty("pluginStateBase64", processor.pluginStateBase64);
    object->setProperty("bypassed", processor.bypassed);
    object->setProperty("wetMix", processor.wetMix);
    object->setProperty("outputTrimDb", processor.outputTrimDb);
    if (processor.superCollider.has_value())
        object->setProperty("superCollider", superColliderScriptToVar(*processor.superCollider));
    return var(object.release());
}

TrackProcessorState processorFromVar(const var& value)
{
    TrackProcessorState processor;
    if (auto* object = value.getDynamicObject())
    {
        processor.kind = intToEnum(object->getProperty("kind"), ProcessorKind::audioUnit);
        processor.name = object->getProperty("name").toString();
        processor.pluginIdentifier = object->getProperty("pluginIdentifier").toString();
        processor.pluginStateBase64 = object->getProperty("pluginStateBase64").toString();
        processor.bypassed = static_cast<bool>(object->getProperty("bypassed"));
        processor.wetMix = object->hasProperty("wetMix")
            ? static_cast<float>(static_cast<double>(object->getProperty("wetMix")))
            : 1.0f;
        processor.outputTrimDb = object->hasProperty("outputTrimDb")
            ? static_cast<float>(static_cast<double>(object->getProperty("outputTrimDb")))
            : 0.0f;
        if (object->hasProperty("superCollider"))
            processor.superCollider = superColliderScriptFromVar(object->getProperty("superCollider"));
    }

    return processor;
}

var pluginAutomationLaneToVar(const PluginAutomationLane& lane)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("slotIndex", lane.slotIndex);
    object->setProperty("parameterIndex", lane.parameterIndex);
    object->setProperty("parameterName", lane.parameterName);
    object->setProperty("displayName", lane.displayName);
    juce::Array<var> points;
    for (const auto& point : lane.points)
        points.add(automationPointToVar(point));
    object->setProperty("points", points);
    return var(object.release());
}

PluginAutomationLane pluginAutomationLaneFromVar(const var& value)
{
    PluginAutomationLane lane;
    if (auto* object = value.getDynamicObject())
    {
        lane.slotIndex = static_cast<int>(object->getProperty("slotIndex"));
        lane.parameterIndex = static_cast<int>(object->getProperty("parameterIndex"));
        lane.parameterName = object->getProperty("parameterName").toString();
        lane.displayName = object->getProperty("displayName").toString();
        if (auto* pointsArray = object->getProperty("points").getArray())
            for (const auto& pointValue : *pointsArray)
                lane.points.push_back(automationPointFromVar(pointValue));
    }
    return lane;
}

var midiGeneratorToVar(const MidiGeneratorState& generator)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("kind", enumToInt(generator.kind));
    object->setProperty("name", generator.name);
    object->setProperty("enabled", generator.enabled);
    object->setProperty("followsTransport", generator.followsTransport);
    if (generator.superCollider.has_value())
        object->setProperty("superCollider", superColliderScriptToVar(*generator.superCollider));
    return var(object.release());
}

MidiGeneratorState midiGeneratorFromVar(const var& value)
{
    MidiGeneratorState generator;
    if (auto* object = value.getDynamicObject())
    {
        generator.kind = intToEnum(object->getProperty("kind"), MidiGeneratorKind::none);
        generator.name = object->getProperty("name").toString();
        generator.enabled = static_cast<bool>(object->getProperty("enabled"));
        generator.followsTransport = static_cast<bool>(object->getProperty("followsTransport"));
        if (object->hasProperty("superCollider"))
            generator.superCollider = superColliderScriptFromVar(object->getProperty("superCollider"));
    }

    return generator;
}

var regionToVar(const Region& region)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("name", region.name);
    object->setProperty("colour", colourToVar(region.colour));
    object->setProperty("kind", enumToInt(region.kind));
    object->setProperty("startBeat", region.startBeat);
    object->setProperty("lengthInBeats", region.lengthInBeats);
    object->setProperty("sourceFilePath", region.sourceFilePath);
    object->setProperty("sourceOffsetSeconds", region.sourceOffsetSeconds);
    object->setProperty("fadeInBeats", region.fadeInBeats);
    object->setProperty("fadeOutBeats", region.fadeOutBeats);
    object->setProperty("gain", region.gain);

    juce::Array<var> notes;
    for (const auto& note : region.midiNotes)
        notes.add(midiNoteToVar(note));
    object->setProperty("midiNotes", notes);
    return var(object.release());
}

Region regionFromVar(const var& value)
{
    Region region;
    if (auto* object = value.getDynamicObject())
    {
        region.name = object->getProperty("name").toString();
        region.colour = colourFromVar(object->getProperty("colour"), juce::Colours::grey);
        region.kind = intToEnum(object->getProperty("kind"), RegionKind::audio);
        region.startBeat = static_cast<double>(object->getProperty("startBeat"));
        region.lengthInBeats = static_cast<double>(object->getProperty("lengthInBeats"));
        region.sourceFilePath = object->getProperty("sourceFilePath").toString();
        region.sourceOffsetSeconds = static_cast<double>(object->getProperty("sourceOffsetSeconds"));
        region.fadeInBeats = static_cast<double>(object->getProperty("fadeInBeats"));
        region.fadeOutBeats = static_cast<double>(object->getProperty("fadeOutBeats"));
        region.gain = static_cast<float>(static_cast<double>(object->getProperty("gain")));

        if (auto* notesArray = object->getProperty("midiNotes").getArray())
            for (const auto& noteValue : *notesArray)
                region.midiNotes.push_back(midiNoteFromVar(noteValue));
    }

    return region;
}

var trackToVar(const TrackState& track)
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty("id", track.id);
    object->setProperty("name", track.name);
    object->setProperty("role", track.role);
    object->setProperty("kind", enumToInt(track.kind));
    object->setProperty("channelMode", enumToInt(track.channelMode));
    object->setProperty("colour", colourToVar(track.colour));
    object->setProperty("armed", track.armed);
    object->setProperty("muted", track.muted);
    object->setProperty("solo", track.solo);
    object->setProperty("selected", track.selected);
    object->setProperty("volume", track.mixer.volume);
    object->setProperty("pan", track.mixer.pan);
    object->setProperty("meterLevel", track.mixer.meterLevel);
    object->setProperty("visibleAutomationLane", enumToInt(track.visibleAutomationLane));
    object->setProperty("automationExpanded", track.automationExpanded);
    object->setProperty("automationWriteMode", enumToInt(track.automationWriteMode));
    object->setProperty("selectedPluginAutomationLaneIndex", track.selectedPluginAutomationLaneIndex);

    juce::Array<var> volumePoints;
    for (const auto& point : track.volumeAutomation)
        volumePoints.add(automationPointToVar(point));
    object->setProperty("volumeAutomation", volumePoints);

    juce::Array<var> panPoints;
    for (const auto& point : track.panAutomation)
        panPoints.add(automationPointToVar(point));
    object->setProperty("panAutomation", panPoints);

    juce::Array<var> pluginLanes;
    for (const auto& lane : track.pluginAutomationLanes)
        pluginLanes.add(pluginAutomationLaneToVar(lane));
    object->setProperty("pluginAutomationLanes", pluginLanes);

    juce::Array<var> regions;
    for (const auto& region : track.regions)
        regions.add(regionToVar(region));
    object->setProperty("regions", regions);

    juce::Array<var> inserts;
    for (const auto& insert : track.inserts)
        inserts.add(processorToVar(insert));
    object->setProperty("inserts", inserts);

    object->setProperty("midiGenerator", midiGeneratorToVar(track.midiGenerator));
    if (track.renderScript.has_value())
        object->setProperty("renderScript", superColliderScriptToVar(*track.renderScript));
    return var(object.release());
}

TrackState trackFromVar(const var& value)
{
    TrackState track;
    if (auto* object = value.getDynamicObject())
    {
        track.id = static_cast<int>(object->getProperty("id"));
        track.name = object->getProperty("name").toString();
        track.role = object->getProperty("role").toString();
        track.kind = intToEnum(object->getProperty("kind"), TrackKind::audio);
        track.channelMode = intToEnum(object->getProperty("channelMode"), TrackChannelMode::stereo);
        track.colour = colourFromVar(object->getProperty("colour"), juce::Colours::grey);
        track.armed = static_cast<bool>(object->getProperty("armed"));
        track.muted = static_cast<bool>(object->getProperty("muted"));
        track.solo = static_cast<bool>(object->getProperty("solo"));
        track.selected = static_cast<bool>(object->getProperty("selected"));
        track.mixer.volume = static_cast<float>(static_cast<double>(object->getProperty("volume")));
        track.mixer.pan = static_cast<float>(static_cast<double>(object->getProperty("pan")));
        track.mixer.meterLevel = static_cast<float>(static_cast<double>(object->getProperty("meterLevel")));
        track.visibleAutomationLane = intToEnum(object->getProperty("visibleAutomationLane"), AutomationLaneMode::volume);
        track.automationExpanded = static_cast<bool>(object->getProperty("automationExpanded"));
        track.automationWriteMode = intToEnum(object->getProperty("automationWriteMode"), AutomationWriteMode::read);
        track.automationWriteTarget = AutomationLaneMode::none;
        track.automationGestureActive = false;
        track.automationLatchActive = false;
        track.selectedPluginAutomationLaneIndex = static_cast<int>(object->getProperty("selectedPluginAutomationLaneIndex"));

        if (auto* array = object->getProperty("volumeAutomation").getArray())
            for (const auto& pointValue : *array)
                track.volumeAutomation.push_back(automationPointFromVar(pointValue));

        if (auto* array = object->getProperty("panAutomation").getArray())
            for (const auto& pointValue : *array)
                track.panAutomation.push_back(automationPointFromVar(pointValue));

        if (auto* array = object->getProperty("pluginAutomationLanes").getArray())
            for (const auto& laneValue : *array)
                track.pluginAutomationLanes.push_back(pluginAutomationLaneFromVar(laneValue));

        if (auto* array = object->getProperty("regions").getArray())
            for (const auto& regionValue : *array)
                track.regions.push_back(regionFromVar(regionValue));

        if (auto* array = object->getProperty("inserts").getArray())
            for (const auto& insertValue : *array)
                track.inserts.push_back(processorFromVar(insertValue));

        if (object->hasProperty("midiGenerator"))
            track.midiGenerator = midiGeneratorFromVar(object->getProperty("midiGenerator"));

        if (object->hasProperty("renderScript"))
            track.renderScript = superColliderScriptFromVar(object->getProperty("renderScript"));
    }

    return track;
}
} // namespace

TrackState* SessionState::getSelectedTrack()
{
    auto it = std::find_if(tracks.begin(), tracks.end(), [this] (auto& track) { return track.id == selectedTrackId; });
    return it != tracks.end() ? &(*it) : nullptr;
}

const TrackState* SessionState::getSelectedTrack() const
{
    auto it = std::find_if(tracks.begin(), tracks.end(), [this] (const auto& track) { return track.id == selectedTrackId; });
    return it != tracks.end() ? &(*it) : nullptr;
}

Region* SessionState::getSelectedRegion()
{
    auto* track = getSelectedTrack();

    if (track == nullptr || track->id != selectedRegionTrackId)
        return nullptr;

    if (selectedRegionIndex < 0 || selectedRegionIndex >= static_cast<int>(track->regions.size()))
        return nullptr;

    return &track->regions[static_cast<size_t>(selectedRegionIndex)];
}

const Region* SessionState::getSelectedRegion() const
{
    const auto* track = getSelectedTrack();

    if (track == nullptr || track->id != selectedRegionTrackId)
        return nullptr;

    if (selectedRegionIndex < 0 || selectedRegionIndex >= static_cast<int>(track->regions.size()))
        return nullptr;

    return &track->regions[static_cast<size_t>(selectedRegionIndex)];
}

void SessionState::selectTrack(int trackId)
{
    selectedTrackId = trackId;

    for (auto& track : tracks)
        track.selected = (track.id == trackId);

    if (selectedRegionTrackId != trackId)
        selectedRegionIndex = -1;
}

void SessionState::selectRegion(int trackId, int regionIndex)
{
    selectTrack(trackId);
    selectedRegionTrackId = trackId;
    selectedRegionIndex = regionIndex;
}

SessionState createDemoSession()
{
    using juce::Colour;

    SessionState session;
    session.transport.playheadBeat = 5.25;

    session.tracks = {
        { 1, "Lead Vox", "Audio Track", TrackKind::audio, TrackChannelMode::stereo, Colour::fromRGB(236, 94, 90), false, false, false, true,
            { 0.84f, -0.05f, 0.0f },
            AutomationLaneMode::volume, true, AutomationWriteMode::read, AutomationLaneMode::none, false, false,
            {
                { 1.0, 0.78f, AutomationPoint::SegmentShape::easeOut },
                { 5.0, 0.92f, AutomationPoint::SegmentShape::linear },
                { 10.0, 0.68f, AutomationPoint::SegmentShape::easeIn },
                { 15.0, 0.86f, AutomationPoint::SegmentShape::linear }
            },
            {
                { 1.0, -0.08f, AutomationPoint::SegmentShape::linear },
                { 7.0, 0.10f, AutomationPoint::SegmentShape::easeOut },
                { 15.0, -0.02f, AutomationPoint::SegmentShape::linear }
            },
            {},
            -1,
            {
                { "Verse Lead", Colour::fromRGB(236, 94, 90), RegionKind::audio, 1.0, 8.0, {}, 0.0, 0.0, 0.0, 1.0f, {} },
                { "Hook Double", Colour::fromRGB(255, 133, 92), RegionKind::audio, 11.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} }
            },
            {
                { ProcessorKind::superColliderFx, "SC Tape Bloom", {}, {}, false, 1.0f, 0.0f,
                    SuperColliderScriptState { "Tape Bloom", "In.ar(inBus, 2).tanh * 0.8", "tapeBloom", "SynthDef(\\\\tapeBloom)", "Audio Track -> SC FX Bus A", "Live SC insert ready", true, false } }
            },
            {},
            std::nullopt },

        { 2, "Pulse Lab", "MIDI Track", TrackKind::midi, TrackChannelMode::stereo, Colour::fromRGB(247, 184, 68), false, false, false, false,
            { 0.81f, 0.0f, 0.0f },
            AutomationLaneMode::volume, false, AutomationWriteMode::read, AutomationLaneMode::none, false, false,
            {
                { 1.0, 0.74f, AutomationPoint::SegmentShape::step },
                { 9.0, 0.88f, AutomationPoint::SegmentShape::linear },
                { 16.0, 0.72f, AutomationPoint::SegmentShape::linear }
            },
            {},
            {},
            -1,
            {
                { "Intro Beat", Colour::fromRGB(247, 184, 68), RegionKind::midi, 1.0, 8.0, {}, 0.0, 0.0, 0.0, 1.0f,
                    makeMidiPhrase({
                        { 36, 0.0, 0.5, 110, false }, { 42, 0.5, 0.5, 92, false }, { 38, 1.0, 0.5, 104, false }, { 42, 1.5, 0.5, 88, false },
                        { 36, 2.0, 0.5, 112, false }, { 42, 2.5, 0.5, 90, false }, { 38, 3.0, 0.5, 102, false }, { 46, 3.5, 0.5, 86, false }
                    }) },
                { "Chorus Lift", Colour::fromRGB(255, 207, 99), RegionKind::generated, 9.0, 8.0, {}, 0.0, 0.0, 0.0, 1.0f,
                    makeMidiPhrase({
                        { 48, 0.0, 1.0, 96, false }, { 55, 1.0, 1.0, 96, false }, { 60, 2.0, 1.0, 102, false }, { 67, 3.0, 1.0, 104, false }
                    }) }
            },
            {},
            { MidiGeneratorKind::superCollider, "SC Euclidean Generator", true, true,
                SuperColliderScriptState { "Euclid 7/12", "Pbind(\\\\degree, Pseq([0, 2, 4, 7], inf), \\\\dur, 0.25)", "", "Pdef(\\\\euclid)", "SC MIDI Clock -> Pulse Lab", "Generating MIDI into track input", true, false } },
            std::nullopt },

        { 3, "Chroma Keys", "Instrument Track", TrackKind::instrument, TrackChannelMode::stereo, Colour::fromRGB(67, 183, 148), false, false, false, false,
            { 0.76f, -0.10f, 0.0f },
            AutomationLaneMode::pan, true, AutomationWriteMode::read, AutomationLaneMode::none, false, false,
            {
                { 1.0, 0.70f, AutomationPoint::SegmentShape::easeIn },
                { 8.0, 0.80f, AutomationPoint::SegmentShape::easeOut },
                { 16.0, 0.76f, AutomationPoint::SegmentShape::linear }
            },
            {
                { 1.0, -0.18f, AutomationPoint::SegmentShape::easeOut },
                { 8.0, 0.12f, AutomationPoint::SegmentShape::step },
                { 16.0, -0.04f, AutomationPoint::SegmentShape::linear }
            },
            {
                { 0, 0, "Instrument Param 1", {},
                    {
                        { 1.0, 0.42f, AutomationPoint::SegmentShape::linear },
                        { 9.0, 0.68f, AutomationPoint::SegmentShape::easeOut },
                        { 16.0, 0.36f, AutomationPoint::SegmentShape::linear }
                    } }
            },
            0,
            {
                { "Main Chords", Colour::fromRGB(67, 183, 148), RegionKind::midi, 1.0, 16.0, {}, 0.0, 0.0, 0.0, 1.0f,
                    makeMidiPhrase({
                        { 60, 0.0, 2.0, 98, false }, { 64, 0.0, 2.0, 92, false }, { 67, 0.0, 2.0, 94, false },
                        { 57, 2.0, 2.0, 96, false }, { 60, 2.0, 2.0, 90, false }, { 64, 2.0, 2.0, 94, false },
                        { 62, 4.0, 2.0, 100, false }, { 65, 4.0, 2.0, 94, false }, { 69, 4.0, 2.0, 96, false }
                    }) }
            },
            {
                { ProcessorKind::audioUnit, "Retro Synth", {}, {}, false, 1.0f, 0.0f, std::nullopt }
            },
            {},
            std::nullopt },

        { 4, "Nebula Scene", "SuperCollider Render", TrackKind::superColliderRender, TrackChannelMode::stereo, juce::Colour::fromRGB(84, 155, 255), false, false, false, false,
            { 0.70f, 0.18f, 0.0f },
            AutomationLaneMode::volume, true, AutomationWriteMode::read, AutomationLaneMode::none, false, false,
            {
                { 1.0, 0.66f, AutomationPoint::SegmentShape::linear },
                { 12.0, 0.84f, AutomationPoint::SegmentShape::easeIn },
                { 22.0, 0.58f, AutomationPoint::SegmentShape::linear }
            },
            {
                { 1.0, 0.14f, AutomationPoint::SegmentShape::linear },
                { 13.0, 0.22f, AutomationPoint::SegmentShape::easeOut },
                { 21.0, 0.05f, AutomationPoint::SegmentShape::linear }
            },
            {},
            -1,
            {
                { "Scene Print", juce::Colour::fromRGB(84, 155, 255), RegionKind::generated, 5.0, 12.0, {}, 0.0, 0.0, 0.0, 1.0f, {} },
                { "Granular Tail", juce::Colour::fromRGB(125, 188, 255), RegionKind::generated, 18.0, 4.0, {}, 0.0, 0.0, 0.0, 1.0f, {} }
            },
            {},
            {},
            SuperColliderScriptState { "Nebula Scene", "Out.ar(0, GVerb.ar(Mix(SinOsc.ar([110, 220, 330], 0, 0.1))))", "nebulaScene", "SynthDef(\\\\nebulaScene)", "SC Render Bus A -> audio stem", "Ready for offline bounce or live preview", true, true } },

        { 5, "FX Print", "Audio Track", TrackKind::audio, TrackChannelMode::stereo, juce::Colour::fromRGB(172, 122, 255), false, true, false, false,
            { 0.62f, 0.0f, 0.0f },
            AutomationLaneMode::volume, false, AutomationWriteMode::read, AutomationLaneMode::none, false, false,
            {
                { 1.0, 0.58f, AutomationPoint::SegmentShape::linear },
                { 14.0, 0.62f, AutomationPoint::SegmentShape::step },
                { 18.0, 0.42f, AutomationPoint::SegmentShape::linear }
            },
            {
                { 1.0, 0.0f, AutomationPoint::SegmentShape::linear },
                { 16.0, -0.12f, AutomationPoint::SegmentShape::easeIn },
                { 18.0, 0.06f, AutomationPoint::SegmentShape::linear }
            },
            {},
            -1,
            {
                { "Reverse Swell", juce::Colour::fromRGB(172, 122, 255), RegionKind::audio, 15.0, 2.0, {}, 0.0, 0.0, 0.0, 1.0f, {} }
            },
            {
                { ProcessorKind::audioUnit, "Channel EQ", {}, {}, false, 1.0f, 0.0f, std::nullopt },
                { ProcessorKind::superColliderFx, "SC Resonant Freeze", {}, {}, true, 0.72f, -2.0f,
                    SuperColliderScriptState { "Resonant Freeze", "FFT(LocalBuf(2048), In.ar(inBus, 2))", "freezeFx", "Ndef(\\\\freezeFx)", "FX Print -> SC FX Bus A", "Bypassed SC insert", true, false } }
            },
            {},
            std::nullopt }
    };

    session.selectTrack(session.selectedTrackId);
    session.selectRegion(1, 0);
    return session;
}

juce::String serialiseSessionToJson(const SessionState& session)
{
    auto root = std::make_unique<DynamicObject>();
    root->setProperty("formatVersion", 1);
    root->setProperty("transportPlaying", session.transport.playing);
    root->setProperty("transportRecording", session.transport.recording);
    root->setProperty("transportBpm", session.transport.bpm);
    root->setProperty("transportPlayheadBeat", session.transport.playheadBeat);
    root->setProperty("transportVisibleBeats", session.transport.visibleBeats);
    root->setProperty("routingSuperColliderServerConnected", session.routing.superColliderServerConnected);
    root->setProperty("routingRenderBusName", session.routing.renderBusName);
    root->setProperty("routingFxBusName", session.routing.fxBusName);
    root->setProperty("routingMidiBusName", session.routing.midiBusName);
    root->setProperty("layoutLeftSidebarWidth", session.layout.leftSidebarWidth);
    root->setProperty("layoutRightSidebarWidth", session.layout.rightSidebarWidth);
    root->setProperty("layoutLowerPaneHeight", session.layout.lowerPaneHeight);
    root->setProperty("layoutLowerPaneExpanded", session.layout.lowerPaneExpanded);
    root->setProperty("layoutLowerPaneModeValue", session.layout.lowerPaneModeValue);
    root->setProperty("layoutAudioSelectionExpanded", session.layout.audioSelectionExpanded);
    root->setProperty("layoutAudioSelectionModeValue", session.layout.audioSelectionModeValue);
    root->setProperty("layoutMidiSelectionExpanded", session.layout.midiSelectionExpanded);
    root->setProperty("layoutMidiSelectionModeValue", session.layout.midiSelectionModeValue);
    root->setProperty("layoutAudioEditorZoom", session.layout.audioEditorZoom);
    root->setProperty("layoutMidiEditorZoom", session.layout.midiEditorZoom);
    root->setProperty("layoutAudioEditorTool", session.layout.audioEditorTool);
    root->setProperty("layoutMidiEditorTool", session.layout.midiEditorTool);
    root->setProperty("selectedTrackId", session.selectedTrackId);
    root->setProperty("selectedRegionTrackId", session.selectedRegionTrackId);
    root->setProperty("selectedRegionIndex", session.selectedRegionIndex);

    juce::Array<var> tracks;
    for (const auto& track : session.tracks)
        tracks.add(trackToVar(track));
    root->setProperty("tracks", tracks);

    return JSON::toString(var(root.release()), true);
}

juce::Result deserialiseSessionFromJson(SessionState& session, const juce::String& jsonText)
{
    const auto parsed = JSON::parse(jsonText);
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail("The project file could not be parsed.");

    SessionState loaded;
    loaded.transport.playing = false;
    loaded.transport.recording = false;
    loaded.transport.bpm = static_cast<double>(object->getProperty("transportBpm"));
    loaded.transport.playheadBeat = static_cast<double>(object->getProperty("transportPlayheadBeat"));
    loaded.transport.visibleBeats = static_cast<double>(object->getProperty("transportVisibleBeats"));
    loaded.routing.superColliderServerConnected = static_cast<bool>(object->getProperty("routingSuperColliderServerConnected"));
    loaded.routing.renderBusName = object->getProperty("routingRenderBusName").toString();
    loaded.routing.fxBusName = object->getProperty("routingFxBusName").toString();
    loaded.routing.midiBusName = object->getProperty("routingMidiBusName").toString();
    loaded.layout.leftSidebarWidth = object->hasProperty("layoutLeftSidebarWidth")
        ? static_cast<int>(object->getProperty("layoutLeftSidebarWidth"))
        : loaded.layout.leftSidebarWidth;
    loaded.layout.rightSidebarWidth = object->hasProperty("layoutRightSidebarWidth")
        ? static_cast<int>(object->getProperty("layoutRightSidebarWidth"))
        : loaded.layout.rightSidebarWidth;
    loaded.layout.lowerPaneHeight = object->hasProperty("layoutLowerPaneHeight")
        ? static_cast<int>(object->getProperty("layoutLowerPaneHeight"))
        : loaded.layout.lowerPaneHeight;
    loaded.layout.lowerPaneExpanded = object->hasProperty("layoutLowerPaneExpanded")
        ? static_cast<bool>(object->getProperty("layoutLowerPaneExpanded"))
        : loaded.layout.lowerPaneExpanded;
    loaded.layout.lowerPaneModeValue = object->hasProperty("layoutLowerPaneModeValue")
        ? static_cast<int>(object->getProperty("layoutLowerPaneModeValue"))
        : (object->hasProperty("layoutLowerPaneMixerMode") && static_cast<bool>(object->getProperty("layoutLowerPaneMixerMode")) ? 1 : 0);
    loaded.layout.audioSelectionExpanded = object->hasProperty("layoutAudioSelectionExpanded")
        ? static_cast<bool>(object->getProperty("layoutAudioSelectionExpanded"))
        : loaded.layout.audioSelectionExpanded;
    loaded.layout.audioSelectionModeValue = object->hasProperty("layoutAudioSelectionModeValue")
        ? static_cast<int>(object->getProperty("layoutAudioSelectionModeValue"))
        : (object->hasProperty("layoutAudioSelectionMixerMode") && static_cast<bool>(object->getProperty("layoutAudioSelectionMixerMode")) ? 1 : 0);
    loaded.layout.midiSelectionExpanded = object->hasProperty("layoutMidiSelectionExpanded")
        ? static_cast<bool>(object->getProperty("layoutMidiSelectionExpanded"))
        : loaded.layout.midiSelectionExpanded;
    loaded.layout.midiSelectionModeValue = object->hasProperty("layoutMidiSelectionModeValue")
        ? static_cast<int>(object->getProperty("layoutMidiSelectionModeValue"))
        : (object->hasProperty("layoutMidiSelectionMixerMode") && static_cast<bool>(object->getProperty("layoutMidiSelectionMixerMode")) ? 1 : 0);
    loaded.layout.audioEditorZoom = object->hasProperty("layoutAudioEditorZoom")
        ? static_cast<float>(static_cast<double>(object->getProperty("layoutAudioEditorZoom")))
        : loaded.layout.audioEditorZoom;
    loaded.layout.midiEditorZoom = object->hasProperty("layoutMidiEditorZoom")
        ? static_cast<float>(static_cast<double>(object->getProperty("layoutMidiEditorZoom")))
        : loaded.layout.midiEditorZoom;
    loaded.layout.audioEditorTool = object->hasProperty("layoutAudioEditorTool")
        ? static_cast<int>(object->getProperty("layoutAudioEditorTool"))
        : loaded.layout.audioEditorTool;
    loaded.layout.midiEditorTool = object->hasProperty("layoutMidiEditorTool")
        ? static_cast<int>(object->getProperty("layoutMidiEditorTool"))
        : loaded.layout.midiEditorTool;
    loaded.selectedTrackId = static_cast<int>(object->getProperty("selectedTrackId"));
    loaded.selectedRegionTrackId = static_cast<int>(object->getProperty("selectedRegionTrackId"));
    loaded.selectedRegionIndex = static_cast<int>(object->getProperty("selectedRegionIndex"));

    auto* tracksArray = object->getProperty("tracks").getArray();
    if (tracksArray == nullptr || tracksArray->isEmpty())
        return juce::Result::fail("The project file does not contain any tracks.");

    for (const auto& trackValue : *tracksArray)
        loaded.tracks.push_back(trackFromVar(trackValue));

    if (loaded.tracks.empty())
        return juce::Result::fail("The project file does not contain any valid tracks.");

    const auto selectedExists = std::any_of(loaded.tracks.begin(), loaded.tracks.end(), [&loaded] (const auto& track) {
        return track.id == loaded.selectedTrackId;
    });

    if (! selectedExists)
        loaded.selectedTrackId = loaded.tracks.front().id;

    loaded.selectTrack(loaded.selectedTrackId);

    if (loaded.selectedRegionIndex >= 0)
    {
        auto* track = loaded.getSelectedTrack();
        if (track == nullptr || loaded.selectedRegionTrackId != loaded.selectedTrackId
            || loaded.selectedRegionIndex >= static_cast<int>(track->regions.size()))
        {
            loaded.selectedRegionTrackId = loaded.selectedTrackId;
            loaded.selectedRegionIndex = -1;
        }
    }

    session = std::move(loaded);
    return juce::Result::ok();
}

juce::Result saveSessionToFile(const SessionState& session, const juce::File& file)
{
    if (! file.getParentDirectory().exists() && ! file.getParentDirectory().createDirectory())
        return juce::Result::fail("Could not create the project folder.");

    if (! file.replaceWithText(serialiseSessionToJson(session)))
        return juce::Result::fail("Could not write the project file.");

    return juce::Result::ok();
}

juce::Result loadSessionFromFile(SessionState& session, const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail("The selected project file does not exist.");

    return deserialiseSessionFromJson(session, file.loadFileAsString());
}

juce::String toDisplayString(TrackKind kind)
{
    switch (kind)
    {
        case TrackKind::audio: return "Audio";
        case TrackKind::midi: return "MIDI";
        case TrackKind::instrument: return "Instrument";
        case TrackKind::superColliderRender: return "SuperCollider Render";
    }

    return "Unknown";
}

juce::String toDisplayString(TrackChannelMode mode)
{
    switch (mode)
    {
        case TrackChannelMode::mono: return "Mono";
        case TrackChannelMode::stereo: return "Stereo";
    }

    return "Stereo";
}

juce::String toDisplayString(ProcessorKind kind)
{
    switch (kind)
    {
        case ProcessorKind::audioUnit: return "Audio Unit";
        case ProcessorKind::superColliderFx: return "SuperCollider FX";
    }

    return "Processor";
}

juce::String toDisplayString(MidiGeneratorKind kind)
{
    switch (kind)
    {
        case MidiGeneratorKind::none: return "None";
        case MidiGeneratorKind::superCollider: return "SuperCollider Generator";
    }

    return "Generator";
}

juce::String toDisplayString(AutomationLaneMode mode)
{
    switch (mode)
    {
        case AutomationLaneMode::none: return "Hidden";
        case AutomationLaneMode::volume: return "Volume";
        case AutomationLaneMode::pan: return "Pan";
        case AutomationLaneMode::plugin: return "Plugin";
    }

    return "Automation";
}

juce::String toDisplayString(AutomationWriteMode mode)
{
    switch (mode)
    {
        case AutomationWriteMode::read: return "Read";
        case AutomationWriteMode::touch: return "Touch";
        case AutomationWriteMode::latch: return "Latch";
    }

    return "Read";
}

juce::String toDisplayString(AutomationPoint::SegmentShape shape)
{
    switch (shape)
    {
        case AutomationPoint::SegmentShape::linear: return "Linear";
        case AutomationPoint::SegmentShape::easeIn: return "Ease In";
        case AutomationPoint::SegmentShape::easeOut: return "Ease Out";
        case AutomationPoint::SegmentShape::step: return "Step";
    }

    return "Shape";
}
} // namespace cigol

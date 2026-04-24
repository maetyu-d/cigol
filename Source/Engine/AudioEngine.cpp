#include "AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace cigol
{
namespace
{
float shapeAutomationT(const float t, const AutomationPoint::SegmentShape shape)
{
    switch (shape)
    {
        case AutomationPoint::SegmentShape::linear: return t;
        case AutomationPoint::SegmentShape::easeIn: return t * t;
        case AutomationPoint::SegmentShape::easeOut: return 1.0f - ((1.0f - t) * (1.0f - t));
        case AutomationPoint::SegmentShape::step: return 0.0f;
    }

    return t;
}

float interpolateAutomationValue(const std::vector<AutomationPoint>& points, const double beat, const float fallback)
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
            return juce::jmap(shapeAutomationT(t, left.shapeToNext), left.value, right.value);
        }
    }

    return points.back().value;
}

float pluginAutomationValueForSlot(const std::vector<PluginAutomationLane>& lanes,
                                   const int slotIndex,
                                   const int parameterIndex,
                                   const double beat,
                                   const float fallback)
{
    const auto laneIt = std::find_if(lanes.begin(), lanes.end(), [slotIndex, parameterIndex] (const auto& lane)
    {
        return lane.slotIndex == slotIndex
            && lane.parameterIndex == parameterIndex
            && ! lane.points.empty();
    });

    if (laneIt == lanes.end())
        return fallback;

    return juce::jlimit(0.0f, 1.0f, interpolateAutomationValue(laneIt->points, beat, fallback));
}

float bipolarPanLeftGain(const float pan)
{
    return pan <= 0.0f ? 1.0f : 1.0f - pan;
}

float bipolarPanRightGain(const float pan)
{
    return pan >= 0.0f ? 1.0f : 1.0f + pan;
}

float bufferPeakLevel(const juce::AudioBuffer<float>& buffer)
{
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    return peak;
}

float midiRegionEnvelope(const double noteBeatOffset,
                         const double noteLengthBeats,
                         const double secondsPerBeat)
{
    if (noteLengthBeats <= 0.0)
        return 0.0f;

    const auto noteDurationSeconds = juce::jmax(0.001, noteLengthBeats * secondsPerBeat);
    const auto noteTimeSeconds = juce::jmax(0.0, noteBeatOffset * secondsPerBeat);
    const auto attackSeconds = juce::jmin(0.02, noteDurationSeconds * 0.2);
    const auto releaseSeconds = juce::jmin(0.12, noteDurationSeconds * 0.3);

    auto envelope = 1.0f;
    if (attackSeconds > 0.0)
        envelope = juce::jmin(envelope, static_cast<float>(juce::jlimit(0.0, 1.0, noteTimeSeconds / attackSeconds)));

    const auto timeToEndSeconds = juce::jmax(0.0, noteDurationSeconds - noteTimeSeconds);
    if (releaseSeconds > 0.0)
        envelope = juce::jmin(envelope, static_cast<float>(juce::jlimit(0.0, 1.0, timeToEndSeconds / releaseSeconds)));

    return envelope;
}

float midiRegionSample(const TrackKind kind,
                       const int pitch,
                       const double noteBeatOffset,
                       const double secondsPerBeat)
{
    const auto noteTimeSeconds = juce::jmax(0.0, noteBeatOffset * secondsPerBeat);
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz(pitch);
    const auto phase = juce::MathConstants<double>::twoPi * frequency * noteTimeSeconds;

    if (kind == TrackKind::instrument)
    {
        const auto saw = static_cast<float>((2.0 * std::fmod(frequency * noteTimeSeconds, 1.0)) - 1.0);
        const auto sine = std::sin(phase);
        return static_cast<float>(0.52 * sine + 0.48 * saw);
    }

    return static_cast<float>(std::sin(phase));
}

class TrackGraphProcessor final : public juce::AudioProcessor
{
public:
    TrackGraphProcessor(const std::vector<AudioEngine::TrackPlaybackState>& playbackStatesToUse,
                        juce::SpinLock& stateLockToUse,
                        size_t trackIndexToUse)
        : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          playbackStates(playbackStatesToUse),
          stateLock(stateLockToUse),
          trackIndex(trackIndexToUse)
    {
    }

    const juce::String getName() const override
    {
        return "TrackProcessor";
    }

    void prepareToPlay(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();

        if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
            return;

        AudioEngine::TrackPlaybackState playbackState;

        {
            const juce::SpinLock::ScopedTryLockType lock(stateLock);

            if (! lock.isLocked() || trackIndex >= playbackStates.size())
                return;

            playbackState = playbackStates[trackIndex];
        }

        juce::ignoreUnused(playbackState);
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getInputChannelName(int channelIndex) const override { return juce::String(channelIndex + 1); }
    const juce::String getOutputChannelName(int channelIndex) const override { return juce::String(channelIndex + 1); }
    bool isInputChannelStereoPair(int) const override { return true; }
    bool isOutputChannelStereoPair(int) const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    const std::vector<AudioEngine::TrackPlaybackState>& playbackStates;
    juce::SpinLock& stateLock;
    size_t trackIndex {};
    double currentSampleRate { 44100.0 };
};
} // namespace

AudioEngine::AudioEngine(SessionState& sessionToUse, SuperColliderBridge& bridgeToUse)
    : session(sessionToUse), superColliderBridge(bridgeToUse)
{
    audioFormatManager.registerBasicFormats();
#if JUCE_PLUGINHOST_AU
    pluginFormatManager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
    scanForPlugins();
    syncTrackPlaybackStates();
    refreshHostedPlugins();
    startTimerHz(24);
    rebuildGraph(currentSampleRate, currentBlockSize);
}

AudioEngine::~AudioEngine()
{
    graph.releaseResources();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    currentBlockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    syncTrackPlaybackStates();
    refreshHostedPlugins();
    rebuildGraph(currentSampleRate, currentBlockSize);
    graph.prepareToPlay(currentSampleRate, currentBlockSize);
}

void AudioEngine::audioDeviceStopped()
{
    graph.releaseResources();
    currentSampleRate = 44100.0;
    currentBlockSize = 512;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    graphBuffer.setSize(juce::jmax(2, numOutputChannels), numSamples, false, false, true);
    graphBuffer.clear();

    for (int channel = 0; channel < juce::jmin(numInputChannels, graphBuffer.getNumChannels()); ++channel)
        if (inputChannelData[channel] != nullptr)
            graphBuffer.copyFrom(channel, 0, inputChannelData[channel], numSamples);

    midiBuffer.clear();
    graph.processBlock(graphBuffer, midiBuffer);
    renderAudioRegions(graphBuffer);
    renderMidiRegions(graphBuffer);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (auto* output = outputChannelData[channel])
        {
            if (channel < graphBuffer.getNumChannels())
                juce::FloatVectorOperations::copy(output, graphBuffer.getReadPointer(channel), numSamples);
            else
                juce::FloatVectorOperations::clear(output, numSamples);
        }
    }
}

juce::String AudioEngine::getEngineSummary() const
{
    return "Audio clips and MIDI regions live | " + superColliderBridge.getConnectionSummary(session) + " | " + getPluginHostingSummary();
}

juce::String AudioEngine::getPluginHostingSummary() const
{
    auto loadedSlots = 0;
    auto loadedSuperColliderBridgeSlots = 0;

    {
        const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);
        for (const auto& runtime : hostedTrackRuntimes)
            for (const auto& slot : runtime.slots)
            {
                ++loadedSlots;
                if (slot.instance != nullptr
                    && slot.instance->getName().containsIgnoreCase("SuperCollider"))
                    ++loadedSuperColliderBridgeSlots;
            }
    }

    return "AU choices " + juce::String(knownPluginList.getNumTypes())
         + " | loaded slots " + juce::String(loadedSlots)
         + " | SC bridge slots " + juce::String(loadedSuperColliderBridgeSlots);
}

std::vector<AudioEngine::LoadablePluginChoice> AudioEngine::getAvailablePluginChoices(const TrackState& track, int slotIndex)
{
    scanForPlugins();

    std::vector<LoadablePluginChoice> choices;
    const auto wantsInstrument = slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi);

    for (const auto& description : knownPluginList.getTypes())
    {
        const auto isInstrument = description.isInstrument;
        if (wantsInstrument)
        {
            if (! isInstrument)
                continue;
        }
        else if (isInstrument)
        {
            continue;
        }

        choices.push_back({ description.name, description.fileOrIdentifier, isInstrument });
    }

    std::sort(choices.begin(), choices.end(), [] (const auto& left, const auto& right) { return left.name < right.name; });
    return choices;
}

std::vector<AudioEngine::AutomatableParameterChoice> AudioEngine::getAutomatableParameters(int trackId) const
{
    std::vector<AutomatableParameterChoice> choices;
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
        return runtime.trackId == trackId;
    });

    if (runtimeIt == hostedTrackRuntimes.end())
        return choices;

    for (const auto& slot : runtimeIt->slots)
    {
        if (slot.instance == nullptr || slot.bypassed)
            continue;

        const auto& parameters = slot.instance->getParameters();
        for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex)
        {
            if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
                choices.push_back({ slot.slotIndex,
                                    parameterIndex,
                                    "Slot " + juce::String(slot.slotIndex + 1) + " / " + parameter->getName(48),
                                    parameter->getValue() });
        }
    }

    return choices;
}

AudioEngine::PluginParameterBindingStatus AudioEngine::getTrackPluginParameterBindingStatus(int trackId, int slotIndex, int parameterIndex) const
{
    PluginParameterBindingStatus status;
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
        return runtime.trackId == trackId;
    });

    if (runtimeIt == hostedTrackRuntimes.end())
        return status;

    const auto slotIt = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [slotIndex] (const auto& slot) {
        return slot.slotIndex == slotIndex && slot.instance != nullptr && ! slot.bypassed;
    });

    if (slotIt == runtimeIt->slots.end())
        return status;

    status.slotAvailable = true;
    const auto& parameters = slotIt->instance->getParameters();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(parameters.size()))
        return status;

    if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
    {
        status.parameterAvailable = true;
        status.resolvedName = "Slot " + juce::String(slotIndex + 1) + " / " + parameter->getName(48);
    }

    return status;
}

float AudioEngine::getTrackPluginParameterValue(int trackId, int slotIndex, int parameterIndex) const
{
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
        return runtime.trackId == trackId;
    });

    if (runtimeIt == hostedTrackRuntimes.end())
        return 0.5f;

    const auto slotIt = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [slotIndex] (const auto& slot) {
        return slot.slotIndex == slotIndex && slot.instance != nullptr && ! slot.bypassed;
    });

    if (slotIt == runtimeIt->slots.end())
        return 0.5f;

    const auto& parameters = slotIt->instance->getParameters();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(parameters.size()))
        return 0.5f;

    if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
        return parameter->getValue();

    return 0.5f;
}

bool AudioEngine::setTrackPluginParameterValue(int trackId, int slotIndex, int parameterIndex, float normalisedValue)
{
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
        return runtime.trackId == trackId;
    });

    if (runtimeIt == hostedTrackRuntimes.end())
        return false;

    const auto slotIt = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [slotIndex] (const auto& slot) {
        return slot.slotIndex == slotIndex && slot.instance != nullptr && ! slot.bypassed;
    });

    if (slotIt == runtimeIt->slots.end())
        return false;

    const auto& parameters = slotIt->instance->getParameters();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(parameters.size()))
        return false;

    if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalisedValue));
        parameter->endChangeGesture();
        return true;
    }

    return false;
}

std::optional<AudioEngine::HostedInsertMeterState> AudioEngine::getHostedInsertMeterState(int trackId, int slotIndex) const
{
    const juce::SpinLock::ScopedLockType lock(hostedInsertMeterLock);

    const auto it = std::find_if(hostedInsertMeterStates.begin(), hostedInsertMeterStates.end(), [trackId, slotIndex] (const auto& state)
    {
        return state.trackId == trackId && state.slotIndex == slotIndex;
    });

    if (it == hostedInsertMeterStates.end())
        return std::nullopt;

    return *it;
}

bool AudioEngine::setTrackSlotBypassed(int trackId, int slotIndex, bool bypassed)
{
    auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [trackId] (const auto& track) { return track.id == trackId; });
    if (trackIt == session.tracks.end() || slotIndex < 0 || slotIndex >= static_cast<int>(trackIt->inserts.size()))
        return false;

    trackIt->inserts[static_cast<size_t>(slotIndex)].bypassed = bypassed;

    {
        const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);
        const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
            return runtime.trackId == trackId;
        });

        if (runtimeIt != hostedTrackRuntimes.end())
            for (auto& slot : runtimeIt->slots)
                if (slot.slotIndex == slotIndex)
                    slot.bypassed = bypassed;
    }

    syncTrackPlaybackStates();
    return true;
}

bool AudioEngine::setTrackSuperColliderInsertMix(int trackId, int slotIndex, float wetMix, float outputTrimDb)
{
    auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [trackId] (const auto& track) { return track.id == trackId; });
    if (trackIt == session.tracks.end() || slotIndex < 0 || slotIndex >= static_cast<int>(trackIt->inserts.size()))
        return false;

    auto& slot = trackIt->inserts[static_cast<size_t>(slotIndex)];
    if (slot.kind != ProcessorKind::superColliderFx || ! slot.superCollider.has_value())
        return false;

    slot.wetMix = juce::jlimit(0.0f, 1.0f, wetMix);
    slot.outputTrimDb = juce::jlimit(-24.0f, 24.0f, outputTrimDb);
    syncTrackPlaybackStates();
    return true;
}

bool AudioEngine::setTrackSlotPlugin(int trackId, int slotIndex, const juce::String& pluginIdentifier)
{
    auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [trackId] (const auto& track) { return track.id == trackId; });
    if (trackIt == session.tracks.end() || slotIndex < 0)
        return false;

    const auto description = findPluginDescription(pluginIdentifier);
    if (description.fileOrIdentifier.isEmpty())
        return false;

    while (slotIndex >= static_cast<int>(trackIt->inserts.size()))
        trackIt->inserts.push_back({});

    auto& slot = trackIt->inserts[static_cast<size_t>(slotIndex)];
    slot.kind = ProcessorKind::audioUnit;
    slot.name = description.name;
    slot.pluginIdentifier = description.fileOrIdentifier;
    slot.pluginStateBase64.clear();
    slot.bypassed = false;
    slot.wetMix = 1.0f;
    slot.outputTrimDb = 0.0f;
    slot.superCollider.reset();

    refreshHostedPlugins();
    return true;
}

void AudioEngine::clearTrackSlotPlugin(int trackId, int slotIndex)
{
    auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [trackId] (const auto& track) { return track.id == trackId; });
    if (trackIt == session.tracks.end() || slotIndex < 0 || slotIndex >= static_cast<int>(trackIt->inserts.size()))
        return;

    auto& slot = trackIt->inserts[static_cast<size_t>(slotIndex)];
    slot.name.clear();
    slot.pluginIdentifier.clear();
    slot.pluginStateBase64.clear();
    slot.bypassed = false;
    slot.wetMix = 1.0f;
    slot.outputTrimDb = 0.0f;
    if (slot.kind == ProcessorKind::audioUnit)
        slot.superCollider.reset();

    refreshHostedPlugins();
}

std::unique_ptr<juce::AudioProcessorEditor> AudioEngine::createTrackSlotEditor(int trackId, int slotIndex)
{
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [trackId] (const auto& runtime) {
        return runtime.trackId == trackId;
    });

    if (runtimeIt == hostedTrackRuntimes.end())
        return {};

    const auto slotIt = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [slotIndex] (const auto& slot) {
        return slot.slotIndex == slotIndex && slot.instance != nullptr && ! slot.bypassed;
    });

    if (slotIt == runtimeIt->slots.end())
        return {};

    if (slotIt->instance->hasEditor())
        return std::unique_ptr<juce::AudioProcessorEditor>(slotIt->instance->createEditor());

    return {};
}

void AudioEngine::syncPluginStatesToSession()
{
    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);

    for (auto& track : session.tracks)
    {
        auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [&track] (const auto& runtime) {
            return runtime.trackId == track.id;
        });

        if (runtimeIt == hostedTrackRuntimes.end())
            continue;

        for (auto& insert : track.inserts)
            if (insert.kind == ProcessorKind::audioUnit)
                insert.pluginStateBase64.clear();

        for (const auto& slot : runtimeIt->slots)
        {
            if (slot.slotIndex < 0
                || slot.slotIndex >= static_cast<int>(track.inserts.size())
                || slot.instance == nullptr)
                continue;

            juce::MemoryBlock stateData;
            slot.instance->getStateInformation(stateData);
            track.inserts[static_cast<size_t>(slot.slotIndex)].pluginStateBase64 = stateData.getSize() > 0
                ? stateData.toBase64Encoding()
                : juce::String();
        }
    }
}

void AudioEngine::reloadSessionState()
{
    syncTrackPlaybackStates();
    refreshHostedPlugins();
    rebuildGraph(currentSampleRate, currentBlockSize);
    graph.prepareToPlay(currentSampleRate, currentBlockSize);
}

void AudioEngine::timerCallback()
{
    syncTrackPlaybackStates();
    updateMeters();

    std::vector<HostedInsertRoutingSnapshot> routingSnapshots;
    {
        const juce::SpinLock::ScopedLockType lock(hostedInsertMeterLock);
        routingSnapshots.reserve(hostedInsertMeterStates.size());
        for (const auto& state : hostedInsertMeterStates)
            routingSnapshots.push_back({ state.trackId, state.slotIndex, state.inputLevel, state.outputLevel, state.active });
    }
    superColliderBridge.updateHostedInsertRouting(session, routingSnapshots);
}

void AudioEngine::scanForPlugins()
{
    if (pluginsScanned)
        return;

    juce::OwnedArray<juce::PluginDescription> discoveredTypes;

    for (auto* format : pluginFormatManager.getFormats())
    {
        const auto identifiers = format->searchPathsForPlugins({}, true, false);

        for (const auto& identifier : identifiers)
            knownPluginList.scanAndAddFile(identifier, true, discoveredTypes, *format);
    }

    pluginsScanned = true;
}

juce::PluginDescription AudioEngine::findPluginDescription(const juce::String& identifier) const
{
    for (const auto& description : knownPluginList.getTypes())
        if (description.fileOrIdentifier == identifier)
            return description;

    return {};
}

juce::PluginDescription AudioEngine::findSuperColliderBridgeDescription() const
{
    for (const auto& description : knownPluginList.getTypes())
    {
        if (description.isInstrument)
            continue;

        if (description.name.containsIgnoreCase("SuperCollider"))
            return description;
    }

    return {};
}

void AudioEngine::refreshHostedPlugins()
{
    scanForPlugins();

    std::vector<HostedTrackRuntime> refreshedRuntimes;
    const auto superColliderBridgeDescription = findSuperColliderBridgeDescription();

    for (const auto& track : session.tracks)
    {
        HostedTrackRuntime trackRuntime;
        trackRuntime.trackId = track.id;

        for (size_t slotIndex = 0; slotIndex < track.inserts.size(); ++slotIndex)
        {
            const auto& insert = track.inserts[slotIndex];
            juce::PluginDescription description;

            if (insert.kind == ProcessorKind::audioUnit)
            {
                if (insert.pluginIdentifier.isEmpty())
                    continue;

                description = findPluginDescription(insert.pluginIdentifier);
            }
            else if (insert.kind == ProcessorKind::superColliderFx && insert.superCollider.has_value())
            {
                description = superColliderBridgeDescription;
            }
            else
            {
                continue;
            }

            if (description.fileOrIdentifier.isEmpty())
                continue;

            juce::String errorMessage;
            auto instance = pluginFormatManager.createPluginInstance(description, currentSampleRate, currentBlockSize, errorMessage);
            if (instance == nullptr)
                continue;

            instance->prepareToPlay(currentSampleRate, currentBlockSize);
            if (insert.pluginStateBase64.isNotEmpty())
            {
                juce::MemoryBlock stateData;
                if (stateData.fromBase64Encoding(insert.pluginStateBase64))
                    instance->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
            }
            HostedPluginRuntime runtime;
            runtime.slotIndex = static_cast<int>(slotIndex);
            runtime.identifier = insert.pluginIdentifier;
            runtime.isInstrument = description.isInstrument;
            runtime.bypassed = insert.bypassed;
            runtime.instance = std::move(instance);
            trackRuntime.slots.push_back(std::move(runtime));
        }

        refreshedRuntimes.push_back(std::move(trackRuntime));
    }

    const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);
    hostedTrackRuntimes = std::move(refreshedRuntimes);
}

void AudioEngine::rebuildGraph(double sampleRate, int blockSize)
{
    graph.clear();
    trackNodeIds.clear();

    auto outputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (outputNode == nullptr)
        return;

    audioOutputNodeId = outputNode->nodeID;

    for (size_t trackIndex = 0; trackIndex < session.tracks.size(); ++trackIndex)
    {
        auto trackNode = graph.addNode(std::make_unique<TrackGraphProcessor>(trackPlaybackStates, trackStateLock, trackIndex));

        if (trackNode == nullptr)
            continue;

        trackNodeIds.push_back(trackNode->nodeID);
        const auto leftConnected = graph.addConnection({ { trackNode->nodeID, 0 }, { audioOutputNodeId, 0 } });
        const auto rightConnected = graph.addConnection({ { trackNode->nodeID, 1 }, { audioOutputNodeId, 1 } });

        if (! (leftConnected && rightConnected))
            trackNodeIds.pop_back();
    }

    graph.prepareToPlay(sampleRate, blockSize);
}

void AudioEngine::syncTrackPlaybackStates()
{
    const juce::SpinLock::ScopedLockType lock(trackStateLock);
    trackPlaybackStates.clear();
    trackPlaybackStates.reserve(session.tracks.size());

    for (const auto& track : session.tracks)
    {
        TrackPlaybackState state;
        state.trackId = track.id;
        state.kind = track.kind;
        state.channelMode = track.channelMode;
        state.transportPlaying = session.transport.playing;
        state.muted = track.muted;
        state.hasSuperColliderMidiGenerator = track.midiGenerator.kind == MidiGeneratorKind::superCollider
            && track.midiGenerator.enabled;
        state.hasActiveSuperColliderFx = std::any_of(track.inserts.begin(), track.inserts.end(), [] (const auto& insert) {
            return insert.kind == ProcessorKind::superColliderFx && ! insert.bypassed;
        });
        state.bpm = session.transport.bpm;
        state.playheadBeat = session.transport.playheadBeat;
        state.volume = track.mixer.volume;
        state.pan = track.mixer.pan;
        state.volumeAutomation = track.volumeAutomation;
        state.panAutomation = track.panAutomation;
        state.pluginAutomationLanes = track.pluginAutomationLanes;
        state.selectedPluginAutomationLaneIndex = track.selectedPluginAutomationLaneIndex;
        for (size_t slotIndex = 0; slotIndex < track.inserts.size(); ++slotIndex)
        {
            const auto& insert = track.inserts[slotIndex];
            state.inserts.push_back({ static_cast<int>(slotIndex),
                                      insert.kind,
                                      insert.bypassed,
                                      insert.superCollider.has_value(),
                                      insert.wetMix,
                                      insert.outputTrimDb });
        }

        for (const auto& region : track.regions)
        {
            TrackPlaybackState::RegionPlaybackState regionState;
            regionState.kind = region.kind;
            regionState.startBeat = region.startBeat;
            regionState.lengthInBeats = region.lengthInBeats;
            regionState.sourceOffsetSeconds = region.sourceOffsetSeconds;
            regionState.fadeInBeats = region.fadeInBeats;
            regionState.fadeOutBeats = region.fadeOutBeats;
            regionState.gain = region.gain;
            regionState.sourceFilePath = region.sourceFilePath;
            regionState.clipData = getOrLoadAudioClip(region.sourceFilePath);
            for (const auto& note : region.midiNotes)
                regionState.midiNotes.push_back({ note.pitch, note.startBeat, note.lengthInBeats, note.velocity });
            state.regions.push_back(std::move(regionState));
        }

        trackPlaybackStates.push_back(state);
    }
}

std::shared_ptr<const AudioEngine::AudioClipData> AudioEngine::getOrLoadAudioClip(const juce::String& filePath)
{
    if (filePath.isEmpty())
        return {};

    if (const auto it = audioClipCache.find(filePath); it != audioClipCache.end())
        return it->second;

    juce::File file(filePath);
    if (! file.existsAsFile())
        return {};

    std::unique_ptr<juce::AudioFormatReader> reader(audioFormatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    auto clip = std::make_shared<AudioClipData>();
    clip->filePath = filePath;
    clip->sampleRate = reader->sampleRate;
    clip->samples.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&clip->samples, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    audioClipCache[filePath] = clip;
    return clip;
}

void AudioEngine::renderAudioRegions(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0 || currentSampleRate <= 0.0)
        return;

    std::vector<TrackPlaybackState> playbackStatesCopy;

    {
        const juce::SpinLock::ScopedLockType lock(trackStateLock);
        playbackStatesCopy = trackPlaybackStates;
    }

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    if (left == nullptr)
        return;

    juce::AudioBuffer<float> trackBuffer(2, buffer.getNumSamples());
    juce::AudioBuffer<float> superColliderSendBuffer(2, buffer.getNumSamples());
    juce::MidiBuffer pluginMidi;
    std::vector<HostedInsertMeterState> meterUpdates;

    for (const auto& track : playbackStatesCopy)
    {
        if (! track.transportPlaying || track.muted || track.kind != TrackKind::audio)
            continue;

        trackBuffer.clear();
        const auto secondsPerBeat = 60.0 / juce::jmax(1.0, track.bpm);
        for (const auto& region : track.regions)
        {
            if (region.kind != RegionKind::audio || region.clipData == nullptr || region.lengthInBeats <= 0.0)
                continue;

            const auto regionStartBeat = region.startBeat;
            const auto regionEndBeat = region.startBeat + region.lengthInBeats;
            const auto blockStartBeat = track.playheadBeat;
            const auto blockEndBeat = blockStartBeat + (static_cast<double>(buffer.getNumSamples()) / currentSampleRate) / secondsPerBeat;

            if (blockEndBeat <= regionStartBeat || blockStartBeat >= regionEndBeat)
                continue;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto sampleBeat = blockStartBeat + (static_cast<double>(sample) / currentSampleRate) / secondsPerBeat;
                if (sampleBeat < regionStartBeat || sampleBeat >= regionEndBeat)
                    continue;

                const auto sourceTimeSeconds = region.sourceOffsetSeconds + ((sampleBeat - regionStartBeat) * secondsPerBeat);
                const auto sourcePosition = sourceTimeSeconds * region.clipData->sampleRate;
                const auto sampleIndex = static_cast<int>(sourcePosition);
                const auto alpha = static_cast<float>(sourcePosition - static_cast<double>(sampleIndex));
                const auto regionBeatOffset = sampleBeat - regionStartBeat;
                const auto distanceFromEndBeats = regionEndBeat - sampleBeat;
                const auto automatedVolume = interpolateAutomationValue(track.volumeAutomation, sampleBeat, track.volume);
                const auto automatedPan = interpolateAutomationValue(track.panAutomation, sampleBeat, track.pan);
                const auto leftGain = static_cast<float>(automatedVolume * bipolarPanLeftGain(automatedPan));
                const auto rightGain = static_cast<float>(automatedVolume * bipolarPanRightGain(automatedPan));

                if (sampleIndex < 0 || sampleIndex + 1 >= region.clipData->samples.getNumSamples())
                    continue;

                const auto sampleLeftA = region.clipData->samples.getSample(0, sampleIndex);
                const auto sampleLeftB = region.clipData->samples.getSample(0, sampleIndex + 1);
                auto dryLeft = juce::jmap(alpha, sampleLeftA, sampleLeftB);

                float dryRight = dryLeft;
                if (region.clipData->samples.getNumChannels() > 1)
                {
                    const auto sampleRightA = region.clipData->samples.getSample(1, sampleIndex);
                    const auto sampleRightB = region.clipData->samples.getSample(1, sampleIndex + 1);
                    dryRight = juce::jmap(alpha, sampleRightA, sampleRightB);
                }

                if (track.channelMode == TrackChannelMode::mono)
                {
                    const auto monoSample = 0.5f * (dryLeft + dryRight);
                    dryLeft = monoSample;
                    dryRight = monoSample;
                }

                auto fadeGain = 1.0f;
                if (region.fadeInBeats > 0.0)
                    fadeGain = juce::jmin(fadeGain,
                                          static_cast<float>(juce::jlimit(0.0, 1.0, regionBeatOffset / region.fadeInBeats)));
                if (region.fadeOutBeats > 0.0)
                    fadeGain = juce::jmin(fadeGain,
                                          static_cast<float>(juce::jlimit(0.0, 1.0, distanceFromEndBeats / region.fadeOutBeats)));

                trackBuffer.addSample(0, sample, dryLeft * leftGain * fadeGain * region.gain);
                trackBuffer.addSample(1, sample, dryRight * rightGain * fadeGain * region.gain);
            }
        }

        {
            const juce::SpinLock::ScopedTryLockType lock(pluginRuntimeLock);

            if (lock.isLocked())
            {
                const auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [&track] (const auto& runtime) {
                    return runtime.trackId == track.trackId;
                });

                if (runtimeIt != hostedTrackRuntimes.end())
                {
                    pluginMidi.clear();
                    for (auto& slot : runtimeIt->slots)
                    {
                        if (slot.bypassed || slot.instance == nullptr || slot.isInstrument)
                            continue;

                        const auto insertIt = std::find_if(track.inserts.begin(), track.inserts.end(), [&slot] (const auto& insert) {
                            return insert.slotIndex == slot.slotIndex;
                        });

                        const auto& parameters = slot.instance->getParameters();
                        for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex)
                            if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
                                parameter->setValueNotifyingHost(pluginAutomationValueForSlot(track.pluginAutomationLanes,
                                                                                              slot.slotIndex,
                                                                                              parameterIndex,
                                                                                              track.playheadBeat,
                                                                                              parameter->getValue()));

                        if (insertIt != track.inserts.end()
                            && insertIt->kind == ProcessorKind::superColliderFx
                            && insertIt->hasSuperColliderState)
                        {
                            superColliderSendBuffer.makeCopyOf(trackBuffer, true);
                            const auto inputLevel = bufferPeakLevel(superColliderSendBuffer);
                            slot.instance->processBlock(superColliderSendBuffer, pluginMidi);

                            const auto wetMix = juce::jlimit(0.0f, 1.0f, insertIt->wetMix);
                            const auto dryMix = 1.0f - wetMix;
                            const auto trimGain = juce::Decibels::decibelsToGain(insertIt->outputTrimDb);
                            superColliderSendBuffer.applyGain(trimGain);
                            const auto outputLevel = bufferPeakLevel(superColliderSendBuffer);

                            trackBuffer.applyGain(dryMix);
                            for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), superColliderSendBuffer.getNumChannels()); ++channel)
                                trackBuffer.addFrom(channel, 0, superColliderSendBuffer, channel, 0, superColliderSendBuffer.getNumSamples(), wetMix);

                            meterUpdates.push_back({ track.trackId, slot.slotIndex, inputLevel, outputLevel, true });
                        }
                        else
                        {
                            slot.instance->processBlock(trackBuffer, pluginMidi);
                        }
                    }
                }
            }
        }

        juce::FloatVectorOperations::add(left, trackBuffer.getReadPointer(0), buffer.getNumSamples());
        if (right != nullptr)
            juce::FloatVectorOperations::add(right, trackBuffer.getReadPointer(1), buffer.getNumSamples());
    }

    {
        const juce::SpinLock::ScopedLockType lock(hostedInsertMeterLock);
        hostedInsertMeterStates = std::move(meterUpdates);
    }
}

void AudioEngine::renderMidiRegions(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0 || currentSampleRate <= 0.0)
        return;

    std::vector<TrackPlaybackState> playbackStatesCopy;

    {
        const juce::SpinLock::ScopedLockType lock(trackStateLock);
        playbackStatesCopy = trackPlaybackStates;
    }

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    if (left == nullptr)
        return;

    juce::AudioBuffer<float> trackBuffer(2, buffer.getNumSamples());
    juce::MidiBuffer trackMidi;

    for (const auto& track : playbackStatesCopy)
    {
        if (! track.transportPlaying || track.muted)
            continue;

        if (track.kind != TrackKind::instrument && track.kind != TrackKind::midi)
            continue;

        const auto secondsPerBeat = 60.0 / juce::jmax(1.0, track.bpm);
        const auto trackGainTrim = track.kind == TrackKind::instrument ? 0.16f : 0.12f;
        trackBuffer.clear();
        trackMidi.clear();

        HostedPluginRuntime* instrumentRuntime = nullptr;

        {
            const juce::SpinLock::ScopedTryLockType lock(pluginRuntimeLock);

            if (lock.isLocked())
            {
                auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [&track] (const auto& runtime) {
                    return runtime.trackId == track.trackId;
                });

                if (runtimeIt != hostedTrackRuntimes.end())
                {
                    auto instrumentIt = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [] (const auto& slot) {
                        return slot.instance != nullptr && slot.isInstrument && ! slot.bypassed;
                    });

                    if (instrumentIt != runtimeIt->slots.end())
                        instrumentRuntime = &(*instrumentIt);
                }
            }
        }

        for (const auto& region : track.regions)
        {
            if ((region.kind != RegionKind::midi && region.kind != RegionKind::generated)
                || region.midiNotes.empty()
                || region.lengthInBeats <= 0.0)
                continue;

            const auto regionStartBeat = region.startBeat;
            const auto regionEndBeat = region.startBeat + region.lengthInBeats;
            const auto blockStartBeat = track.playheadBeat;
            const auto blockEndBeat = blockStartBeat + (static_cast<double>(buffer.getNumSamples()) / currentSampleRate) / secondsPerBeat;

            if (blockEndBeat <= regionStartBeat || blockStartBeat >= regionEndBeat)
                continue;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto sampleBeat = blockStartBeat + (static_cast<double>(sample) / currentSampleRate) / secondsPerBeat;
                if (sampleBeat < regionStartBeat || sampleBeat >= regionEndBeat)
                    continue;

                const auto automatedVolume = interpolateAutomationValue(track.volumeAutomation, sampleBeat, track.volume);
                const auto automatedPan = interpolateAutomationValue(track.panAutomation, sampleBeat, track.pan);
                const auto leftGain = static_cast<float>(automatedVolume * bipolarPanLeftGain(automatedPan));
                const auto rightGain = static_cast<float>(automatedVolume * bipolarPanRightGain(automatedPan));
                const auto regionBeatOffset = sampleBeat - regionStartBeat;

                auto mixedSample = 0.0f;

                for (const auto& note : region.midiNotes)
                {
                    const auto noteStartBeat = note.startBeat;
                    const auto noteEndBeat = note.startBeat + note.lengthInBeats;

                    if (regionBeatOffset < noteStartBeat || regionBeatOffset >= noteEndBeat)
                        continue;

                    const auto noteBeatOffset = regionBeatOffset - noteStartBeat;
                    const auto velocityGain = static_cast<float>(note.velocity / 127.0f);
                    const auto envelope = midiRegionEnvelope(noteBeatOffset, note.lengthInBeats, secondsPerBeat);

                    mixedSample += midiRegionSample(track.kind, note.pitch, noteBeatOffset, secondsPerBeat)
                                   * velocityGain * envelope;
                }

                if (mixedSample == 0.0f)
                    continue;

                mixedSample = std::tanh(mixedSample) * trackGainTrim * region.gain;
                if (instrumentRuntime == nullptr)
                {
                    trackBuffer.addSample(0, sample, mixedSample * leftGain);
                    trackBuffer.addSample(1, sample, mixedSample * rightGain);
                }
            }

            if (instrumentRuntime != nullptr)
            {
                auto& activePitches = activeMidiNotesByTrack[track.trackId];

                for (const auto& note : region.midiNotes)
                {
                    const auto noteStartBeat = region.startBeat + note.startBeat;
                    const auto noteEndBeat = noteStartBeat + note.lengthInBeats;

                    if (blockStartBeat <= noteStartBeat && noteStartBeat < blockEndBeat)
                    {
                        const auto samplePosition = juce::jlimit(0,
                                                                 buffer.getNumSamples() - 1,
                                                                 static_cast<int>(((noteStartBeat - blockStartBeat) * secondsPerBeat) * currentSampleRate));
                        trackMidi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, static_cast<juce::uint8>(note.velocity)), samplePosition);
                        activePitches.insert(note.pitch);
                    }
                    else if (noteStartBeat < blockStartBeat && blockStartBeat < noteEndBeat && activePitches.count(note.pitch) == 0)
                    {
                        trackMidi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, static_cast<juce::uint8>(note.velocity)), 0);
                        activePitches.insert(note.pitch);
                    }

                    if (blockStartBeat <= noteEndBeat && noteEndBeat < blockEndBeat)
                    {
                        const auto samplePosition = juce::jlimit(0,
                                                                 buffer.getNumSamples() - 1,
                                                                 static_cast<int>(((noteEndBeat - blockStartBeat) * secondsPerBeat) * currentSampleRate));
                        trackMidi.addEvent(juce::MidiMessage::noteOff(1, note.pitch), samplePosition);
                        activePitches.erase(note.pitch);
                    }
                }
            }
        }

        if (instrumentRuntime != nullptr)
        {
            const juce::SpinLock::ScopedTryLockType lock(pluginRuntimeLock);

            if (lock.isLocked())
            {
                auto runtimeIt = std::find_if(hostedTrackRuntimes.begin(), hostedTrackRuntimes.end(), [&track] (const auto& runtime) {
                    return runtime.trackId == track.trackId;
                });

                if (runtimeIt != hostedTrackRuntimes.end())
                {
                    auto hostedInstrument = std::find_if(runtimeIt->slots.begin(), runtimeIt->slots.end(), [] (const auto& slot) {
                        return slot.instance != nullptr && slot.isInstrument && ! slot.bypassed;
                    });

                    if (hostedInstrument != runtimeIt->slots.end())
                    {
                        const auto& instrumentParameters = hostedInstrument->instance->getParameters();
                        for (int parameterIndex = 0; parameterIndex < static_cast<int>(instrumentParameters.size()); ++parameterIndex)
                            if (auto* parameter = instrumentParameters[static_cast<size_t>(parameterIndex)])
                                parameter->setValueNotifyingHost(pluginAutomationValueForSlot(track.pluginAutomationLanes,
                                                                                              hostedInstrument->slotIndex,
                                                                                              parameterIndex,
                                                                                              track.playheadBeat,
                                                                                              parameter->getValue()));

                        hostedInstrument->instance->processBlock(trackBuffer, trackMidi);
                        trackMidi.clear();

                        for (auto& slot : runtimeIt->slots)
                        {
                            if (slot.bypassed || slot.instance == nullptr || slot.isInstrument)
                                continue;

                            const auto& parameters = slot.instance->getParameters();
                            for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex)
                                if (auto* parameter = parameters[static_cast<size_t>(parameterIndex)])
                                    parameter->setValueNotifyingHost(pluginAutomationValueForSlot(track.pluginAutomationLanes,
                                                                                                  slot.slotIndex,
                                                                                                  parameterIndex,
                                                                                                  track.playheadBeat,
                                                                                                  parameter->getValue()));

                            slot.instance->processBlock(trackBuffer, trackMidi);
                        }
                    }
                }
            }
        }

        juce::FloatVectorOperations::add(left, trackBuffer.getReadPointer(0), buffer.getNumSamples());
        if (right != nullptr)
            juce::FloatVectorOperations::add(right, trackBuffer.getReadPointer(1), buffer.getNumSamples());
    }
}

void AudioEngine::updateMeters()
{
    const auto phaseSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;

    for (size_t i = 0; i < session.tracks.size(); ++i)
    {
        const auto& track = session.tracks[i];
        const auto active = session.transport.playing && ! track.muted;
        const auto modulation = 0.5 + 0.5 * std::sin(phaseSeconds * (0.8 + 0.17 * static_cast<double>(i)));
        const auto boosted = track.kind == TrackKind::superColliderRender ? 0.85 : 0.65;
        const auto insertLift = std::any_of(track.inserts.begin(), track.inserts.end(), [] (const auto& insert) {
            return insert.kind == ProcessorKind::superColliderFx && ! insert.bypassed;
        }) ? 0.12 : 0.0;

        session.tracks[i].mixer.meterLevel = active
            ? juce::jlimit(0.0f, 1.0f, static_cast<float>((modulation * (boosted + insertLift)) * track.mixer.volume))
            : 0.0f;
    }
}
} // namespace cigol

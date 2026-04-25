#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <map>

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

double beatForProjectSeconds(const SessionState& session,
                             const double targetSeconds,
                             const double lowBeat,
                             const double highBeat)
{
    auto low = lowBeat;
    auto high = juce::jmax(lowBeat, highBeat);

    for (int i = 0; i < 32; ++i)
    {
        const auto mid = 0.5 * (low + high);
        if (projectSecondsForBeat(session, mid) < targetSeconds)
            low = mid;
        else
            high = mid;
    }

    return 0.5 * (low + high);
}

juce::AudioFormat* formatForRenderFile(juce::AudioFormatManager& manager, const juce::File& targetFile)
{
    const auto extension = targetFile.getFileExtension().toLowerCase();
    if (extension == ".aiff" || extension == ".aif")
        return manager.findFormatForFileExtension("aiff");

    return manager.findFormatForFileExtension("wav");
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
    loadPluginCatalogCache();
    syncTrackPlaybackStates();
    startTimerHz(24);
    rebuildGraph(currentSampleRate, currentBlockSize);
    if (getRememberedPluginCatalog().empty())
        requestPluginRescan();
}

AudioEngine::~AudioEngine()
{
    joinCompletedPluginScanThread();
    if (pluginScanThread.joinable())
        pluginScanThread.join();
    graph.releaseResources();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    currentBlockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    syncTrackPlaybackStates();
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
    auto rememberedChoices = 0;
    auto activeChoices = 0;

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

    {
        const juce::ScopedLock lock(pluginCatalogLock);
        rememberedChoices = static_cast<int>(cachedPluginDescriptions.size());
        activeChoices = static_cast<int>(std::count_if(cachedPluginDescriptions.begin(),
                                                       cachedPluginDescriptions.end(),
                                                       [] (const auto& entry) { return entry.enabled; }));
    }

    return "AU choices " + juce::String(activeChoices) + " active / " + juce::String(rememberedChoices) + " remembered"
         + " | loaded slots " + juce::String(loadedSlots)
         + " | SC bridge slots " + juce::String(loadedSuperColliderBridgeSlots);
}

std::vector<AudioEngine::LoadablePluginChoice> AudioEngine::getAvailablePluginChoices(const TrackState& track, int slotIndex)
{
    std::vector<LoadablePluginChoice> choices;
    const auto wantsInstrument = slotIndex == 0 && (track.kind == TrackKind::instrument || track.kind == TrackKind::midi);

    const juce::ScopedLock lock(pluginCatalogLock);
    for (const auto& entry : cachedPluginDescriptions)
    {
        if (! entry.enabled)
            continue;

        const auto& description = entry.description;
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

std::vector<AudioEngine::RememberedPluginEntry> AudioEngine::getRememberedPluginCatalog() const
{
    std::vector<RememberedPluginEntry> remembered;
    const juce::ScopedLock lock(pluginCatalogLock);
    remembered.reserve(cachedPluginDescriptions.size());
    for (const auto& entry : cachedPluginDescriptions)
        remembered.push_back({ entry.description.name,
                               entry.description.fileOrIdentifier,
                               entry.description.isInstrument,
                               entry.enabled });

    std::sort(remembered.begin(), remembered.end(), [] (const auto& left, const auto& right) { return left.name < right.name; });
    return remembered;
}

juce::String AudioEngine::getPluginScanStatus() const
{
    const juce::ScopedLock lock(pluginCatalogLock);
    return pluginScanStatus;
}

bool AudioEngine::isPluginScanInProgress() const
{
    return pluginScanInProgress.load();
}

void AudioEngine::requestPluginRescan()
{
    if (pluginScanInProgress.exchange(true))
        return;

    joinCompletedPluginScanThread();

    {
        const juce::ScopedLock lock(pluginCatalogLock);
        pluginScanStatus = "Scanning plugins in the background...";
        pendingPluginScanStatus.clear();
        pendingCachedPluginDescriptions.clear();
    }

    pluginScanResultsPending = false;

    pluginScanThread = std::thread([this]
    {
        std::map<juce::String, bool> enabledByIdentifier;
        {
            const juce::ScopedLock lock(pluginCatalogLock);
            for (const auto& entry : cachedPluginDescriptions)
                enabledByIdentifier[entry.description.fileOrIdentifier] = entry.enabled;
        }

        juce::AudioPluginFormatManager localFormatManager;
#if JUCE_PLUGINHOST_AU
        localFormatManager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif

        juce::KnownPluginList localKnownPluginList;
        juce::OwnedArray<juce::PluginDescription> discoveredTypes;

        for (auto* format : localFormatManager.getFormats())
        {
            const auto identifiers = format->searchPathsForPlugins({}, true, false);
            for (const auto& identifier : identifiers)
                localKnownPluginList.scanAndAddFile(identifier, true, discoveredTypes, *format);
        }

        std::vector<CachedPluginDescription> scannedDescriptions;
        scannedDescriptions.reserve(static_cast<size_t>(localKnownPluginList.getNumTypes()));
        for (const auto& description : localKnownPluginList.getTypes())
        {
            const auto enabledIt = enabledByIdentifier.find(description.fileOrIdentifier);
            scannedDescriptions.push_back({ description,
                                            enabledIt != enabledByIdentifier.end() ? enabledIt->second : true });
        }

        {
            const juce::ScopedLock lock(pluginCatalogLock);
            pendingCachedPluginDescriptions = std::move(scannedDescriptions);
            pendingPluginScanStatus = "Remembered " + juce::String(pendingCachedPluginDescriptions.size()) + " plugins";
        }

        pluginScanResultsPending = true;
        pluginScanInProgress = false;
    });
}

bool AudioEngine::setPluginEnabled(const juce::String& pluginIdentifier, bool enabled)
{
    auto changed = false;
    {
        const juce::ScopedLock lock(pluginCatalogLock);
        for (auto& entry : cachedPluginDescriptions)
            if (entry.description.fileOrIdentifier == pluginIdentifier)
            {
                if (entry.enabled != enabled)
                {
                    entry.enabled = enabled;
                    pluginScanStatus = "Remembered " + juce::String(cachedPluginDescriptions.size()) + " plugins";
                    changed = true;
                }
                break;
            }
    }

    if (! changed)
        return false;

    savePluginCatalogCache();
    refreshHostedPlugins();
    return true;
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
            if (auto* parameter = parameters[parameterIndex])
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

    if (auto* parameter = parameters[parameterIndex])
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

    if (auto* parameter = parameters[parameterIndex])
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

    if (auto* parameter = parameters[parameterIndex])
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
    for (const auto& track : session.tracks)
        for (const auto& region : track.regions)
            if (region.kind == RegionKind::audio && region.warpEnabled && region.sourceFilePath.isNotEmpty())
            {
                const auto targetDurationSeconds = juce::jmax(0.001,
                                                              projectSecondsForBeat(session, region.startBeat + region.lengthInBeats)
                                                                  - projectSecondsForBeat(session, region.startBeat));
                prepareWarpedAudioClip(region, targetDurationSeconds);
            }
    if (! deferredPluginInitialisationPending)
        refreshHostedPlugins();
    rebuildGraph(currentSampleRate, currentBlockSize);
    graph.prepareToPlay(currentSampleRate, currentBlockSize);
}

bool AudioEngine::renderOfflineToFile(const OfflineRenderRequest& request, const juce::File& targetFile, juce::String& message)
{
    const auto startBeat = juce::jmax(1.0, request.startBeat);
    const auto endBeat = juce::jmax(startBeat + 0.001, request.endBeat);
    if (targetFile == juce::File())
    {
        message = "No bounce destination was chosen.";
        return false;
    }

    if (! targetFile.getParentDirectory().exists() && ! targetFile.getParentDirectory().createDirectory())
    {
        message = "Couldn't create the bounce folder.";
        return false;
    }

    syncPluginStatesToSession();
    syncTrackPlaybackStates();

    const auto sampleRate = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const auto blockSize = juce::jmax(128, currentBlockSize);
    const auto startSeconds = projectSecondsForBeat(session, startBeat);
    const auto endSeconds = projectSecondsForBeat(session, endBeat);
    const auto durationSeconds = juce::jmax(0.001, endSeconds - startSeconds) + juce::jmax(0.0, request.tailSeconds);
    const auto totalSamples = juce::jmax<int64_t>(1, static_cast<int64_t>(std::ceil(durationSeconds * sampleRate)));

    std::unique_ptr<juce::OutputStream> outputStream(targetFile.createOutputStream());
    if (outputStream == nullptr)
    {
        message = "Couldn't open the bounce file for writing.";
        return false;
    }

    auto* format = formatForRenderFile(audioFormatManager, targetFile);
    if (format == nullptr)
    {
        message = "That audio format isn't available right now.";
        return false;
    }

    auto writerOptions = juce::AudioFormatWriterOptions()
        .withSampleRate(sampleRate)
        .withNumChannels(2)
        .withBitsPerSample(24);
    std::unique_ptr<juce::AudioFormatWriter> writer(format->createWriterFor(outputStream, writerOptions));
    if (writer == nullptr)
    {
        message = "Couldn't start the bounce writer.";
        return false;
    }

    std::vector<TrackPlaybackState> basePlaybackStates;
    {
        const juce::SpinLock::ScopedLockType lock(trackStateLock);
        basePlaybackStates = trackPlaybackStates;
    }

    struct PluginStateSnapshot
    {
        juce::AudioPluginInstance* instance { nullptr };
        juce::MemoryBlock state;
    };

    std::vector<PluginStateSnapshot> pluginSnapshots;
    {
        const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);
        for (auto& runtime : hostedTrackRuntimes)
            for (auto& slot : runtime.slots)
                if (slot.instance != nullptr)
                {
                    pluginSnapshots.push_back({ slot.instance.get(), {} });
                    slot.instance->getStateInformation(pluginSnapshots.back().state);
                    slot.instance->reset();
                    slot.instance->prepareToPlay(sampleRate, blockSize);
                }
    }

    const auto savedMidiNotes = activeMidiNotesByTrack;
    activeMidiNotesByTrack.clear();

    juce::AudioBuffer<float> renderBuffer(2, blockSize);
    juce::AudioBuffer<float> fullRenderBuffer(2, static_cast<int>(totalSamples));
    fullRenderBuffer.clear();
    auto renderedSamples = int64_t { 0 };
    const auto beatSearchEnd = endBeat + juce::jmax(8.0, (durationSeconds * session.transport.bpm) / 60.0);
    while (renderedSamples < totalSamples)
    {
        const auto samplesThisBlock = static_cast<int>(juce::jmin<int64_t>(blockSize, totalSamples - renderedSamples));
        renderBuffer.setSize(2, samplesThisBlock, false, false, true);
        renderBuffer.clear();

        auto blockPlaybackStates = basePlaybackStates;
        const auto blockStartSeconds = startSeconds + (static_cast<double>(renderedSamples) / sampleRate);
        const auto blockStartBeat = beatForProjectSeconds(session, blockStartSeconds, startBeat, beatSearchEnd);
        for (auto& track : blockPlaybackStates)
        {
            track.playheadBeat = blockStartBeat;
            track.projectBpm = projectTempoAtBeat(session.transport, blockStartBeat);
            if (const auto sourceTrack = std::find_if(session.tracks.begin(), session.tracks.end(),
                                                      [&track] (const auto& candidate) { return candidate.id == track.trackId; });
                sourceTrack != session.tracks.end())
            {
                track.bpm = trackTempoAtBeat(session, *sourceTrack, blockStartBeat);
            }
            track.transportPlaying = request.trackId.has_value() ? track.trackId == *request.trackId : true;
        }

        {
            const juce::SpinLock::ScopedLockType lock(trackStateLock);
            trackPlaybackStates = blockPlaybackStates;
        }

        renderAudioRegions(renderBuffer);
        renderMidiRegions(renderBuffer);
        for (int channel = 0; channel < fullRenderBuffer.getNumChannels(); ++channel)
            fullRenderBuffer.copyFrom(channel, static_cast<int>(renderedSamples), renderBuffer, channel, 0, samplesThisBlock);
        renderedSamples += samplesThisBlock;
    }

    if (request.normaliseOutput)
    {
        const auto peak = juce::jmax(fullRenderBuffer.getMagnitude(0, 0, fullRenderBuffer.getNumSamples()),
                                     fullRenderBuffer.getMagnitude(1, 0, fullRenderBuffer.getNumSamples()));
        if (peak > 0.00001f)
            fullRenderBuffer.applyGain(0.96f / peak);
    }

    writer->writeFromAudioSampleBuffer(fullRenderBuffer, 0, fullRenderBuffer.getNumSamples());

    {
        const juce::SpinLock::ScopedLockType lock(pluginRuntimeLock);
        for (auto& snapshot : pluginSnapshots)
            if (snapshot.instance != nullptr)
            {
                snapshot.instance->setStateInformation(snapshot.state.getData(), static_cast<int>(snapshot.state.getSize()));
                snapshot.instance->reset();
                snapshot.instance->prepareToPlay(currentSampleRate > 0.0 ? currentSampleRate : sampleRate,
                                                 juce::jmax(128, currentBlockSize));
            }
    }

    activeMidiNotesByTrack = savedMidiNotes;
    reloadSessionState();

    message = "Bounced audio to " + targetFile.getFileName();
    return true;
}

void AudioEngine::timerCallback()
{
    if (pluginScanResultsPending.exchange(false))
    {
        joinCompletedPluginScanThread();
        commitPendingPluginScanResults();
        refreshHostedPlugins();
    }

    if (deferredPluginInitialisationPending)
    {
        deferredPluginInitialisationPending = false;
        refreshHostedPlugins();
        rebuildGraph(currentSampleRate, currentBlockSize);
        graph.prepareToPlay(currentSampleRate, currentBlockSize);
    }

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

void AudioEngine::loadPluginCatalogCache()
{
    juce::String loadedStatus = "Plugin library idle";
    std::vector<CachedPluginDescription> loadedDescriptions;

    const auto cacheFile = getPluginCatalogCacheFile();
    if (cacheFile.existsAsFile())
        if (const auto parsed = juce::JSON::parse(cacheFile.loadFileAsString()); ! parsed.isVoid())
            if (auto* object = parsed.getDynamicObject())
            {
                if (auto* array = object->getProperty("plugins").getArray())
                    for (const auto& pluginValue : *array)
                        if (auto* pluginObject = pluginValue.getDynamicObject())
                        {
                            const auto xmlText = pluginObject->getProperty("descriptionXml").toString();
                            if (xmlText.isEmpty())
                                continue;

                            std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(xmlText));
                            if (xml == nullptr)
                                continue;

                            juce::PluginDescription description;
                            if (! description.loadFromXml(*xml))
                                continue;

                            loadedDescriptions.push_back({ description,
                                                           ! pluginObject->hasProperty("enabled")
                                                               || static_cast<bool>(pluginObject->getProperty("enabled")) });
                        }

                if (object->hasProperty("status"))
                    loadedStatus = object->getProperty("status").toString();
            }

    const juce::ScopedLock lock(pluginCatalogLock);
    cachedPluginDescriptions = std::move(loadedDescriptions);
    pluginScanStatus = cachedPluginDescriptions.empty()
        ? "No remembered plugins yet. Run a rescan."
        : loadedStatus;
}

void AudioEngine::savePluginCatalogCache() const
{
    juce::Array<juce::var> plugins;
    juce::String statusToSave;
    {
        const juce::ScopedLock lock(pluginCatalogLock);
        statusToSave = pluginScanStatus;
        for (const auto& entry : cachedPluginDescriptions)
        {
            auto pluginObject = std::make_unique<juce::DynamicObject>();
            pluginObject->setProperty("enabled", entry.enabled);
            if (auto xml = std::unique_ptr<juce::XmlElement>(entry.description.createXml()))
                pluginObject->setProperty("descriptionXml", xml->toString());
            plugins.add(juce::var(pluginObject.release()));
        }
    }

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("status", statusToSave);
    root->setProperty("plugins", plugins);

    const auto cacheFile = getPluginCatalogCacheFile();
    cacheFile.getParentDirectory().createDirectory();
    cacheFile.replaceWithText(juce::JSON::toString(juce::var(root.release())));
}

juce::File AudioEngine::getPluginCatalogCacheFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("cigoL")
        .getChildFile("plugin-catalog.json");
}

void AudioEngine::joinCompletedPluginScanThread()
{
    if (pluginScanThread.joinable() && ! pluginScanInProgress.load())
        pluginScanThread.join();
}

void AudioEngine::commitPendingPluginScanResults()
{
    juce::String committedStatus;
    {
        const juce::ScopedLock lock(pluginCatalogLock);
        if (pendingCachedPluginDescriptions.empty() && pendingPluginScanStatus.isEmpty())
            return;

        cachedPluginDescriptions = std::move(pendingCachedPluginDescriptions);
        pendingCachedPluginDescriptions.clear();
        pluginScanStatus = pendingPluginScanStatus.isNotEmpty()
            ? pendingPluginScanStatus
            : ("Remembered " + juce::String(cachedPluginDescriptions.size()) + " plugins");
        pendingPluginScanStatus.clear();
        committedStatus = pluginScanStatus;
    }

    juce::ignoreUnused(committedStatus);
    savePluginCatalogCache();
}

juce::PluginDescription AudioEngine::findPluginDescription(const juce::String& identifier) const
{
    const juce::ScopedLock lock(pluginCatalogLock);
    for (const auto& entry : cachedPluginDescriptions)
        if (entry.enabled && entry.description.fileOrIdentifier == identifier)
            return entry.description;

    return {};
}

juce::PluginDescription AudioEngine::findSuperColliderBridgeDescription() const
{
    const juce::ScopedLock lock(pluginCatalogLock);
    for (const auto& entry : cachedPluginDescriptions)
    {
        if (! entry.enabled)
            continue;

        const auto& description = entry.description;
        if (description.isInstrument)
            continue;

        if (description.name.containsIgnoreCase("SuperCollider"))
            return description;
    }

    return {};
}

void AudioEngine::refreshHostedPlugins()
{
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
        state.projectBpm = session.transport.bpm;
        state.tempoMultiplier = track.tempoMultiplier;
        state.tempoAutomation = session.transport.tempoAutomation;
        state.bpm = trackTempoAtBeat(session, track, session.transport.playheadBeat);
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
            regionState.warpEnabled = region.warpEnabled;
            regionState.sourceDurationSeconds = region.sourceDurationSeconds;
            regionState.loopEnabled = region.loopEnabled;
            regionState.loopLengthInBeats = region.loopLengthInBeats > 0.0 ? region.loopLengthInBeats : region.lengthInBeats;
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

juce::String AudioEngine::createWarpedAudioClipCacheKey(const juce::String& filePath,
                                                        double sourceStartSeconds,
                                                        double sourceDurationSeconds,
                                                        double targetDurationSeconds) const
{
    return filePath
        + "|warp|" + juce::String(sourceStartSeconds, 4)
        + "|" + juce::String(sourceDurationSeconds, 4)
        + "|" + juce::String(targetDurationSeconds, 4);
}

bool AudioEngine::isWarpedAudioClipReady(const Region& region, double targetDurationSeconds)
{
    if (region.sourceFilePath.isEmpty() || targetDurationSeconds <= 0.0)
        return false;

    const auto clipData = getOrLoadAudioClip(region.sourceFilePath);
    if (clipData == nullptr)
        return false;

    const auto sourceClipDurationSeconds = static_cast<double>(clipData->samples.getNumSamples()) / juce::jmax(1.0, clipData->sampleRate);
    const auto sourceStartSeconds = juce::jlimit(0.0, sourceClipDurationSeconds, region.sourceOffsetSeconds);
    const auto explicitDuration = region.sourceDurationSeconds > 0.0 ? region.sourceDurationSeconds : (sourceClipDurationSeconds - sourceStartSeconds);
    const auto sourceDurationSeconds = juce::jlimit(0.001, juce::jmax(0.001, sourceClipDurationSeconds - sourceStartSeconds), explicitDuration);
    const auto cacheKey = createWarpedAudioClipCacheKey(region.sourceFilePath, sourceStartSeconds, sourceDurationSeconds, targetDurationSeconds);
    return warpedAudioClipCache.find(cacheKey) != warpedAudioClipCache.end();
}

void AudioEngine::prepareWarpedAudioClip(const Region& region, double targetDurationSeconds)
{
    if (region.sourceFilePath.isEmpty() || targetDurationSeconds <= 0.0)
        return;

    TrackPlaybackState::RegionPlaybackState regionState;
    regionState.sourceFilePath = region.sourceFilePath;
    regionState.sourceOffsetSeconds = region.sourceOffsetSeconds;
    regionState.sourceDurationSeconds = region.sourceDurationSeconds;
    regionState.clipData = getOrLoadAudioClip(region.sourceFilePath);

    if (regionState.clipData == nullptr)
        return;

    getOrCreateWarpedAudioClip(regionState, targetDurationSeconds);
}

std::shared_ptr<const AudioEngine::AudioClipData> AudioEngine::findWarpedAudioClip(const TrackPlaybackState::RegionPlaybackState& region,
                                                                                    double targetDurationSeconds) const
{
    if (region.clipData == nullptr || targetDurationSeconds <= 0.0)
        return {};

    const auto sourceClipDurationSeconds = static_cast<double>(region.clipData->samples.getNumSamples()) / juce::jmax(1.0, region.clipData->sampleRate);
    const auto sourceStartSeconds = juce::jlimit(0.0, sourceClipDurationSeconds, region.sourceOffsetSeconds);
    const auto explicitDuration = region.sourceDurationSeconds > 0.0 ? region.sourceDurationSeconds : (sourceClipDurationSeconds - sourceStartSeconds);
    const auto sourceDurationSeconds = juce::jlimit(0.001, juce::jmax(0.001, sourceClipDurationSeconds - sourceStartSeconds), explicitDuration);
    const auto cacheKey = createWarpedAudioClipCacheKey(region.sourceFilePath, sourceStartSeconds, sourceDurationSeconds, targetDurationSeconds);

    if (const auto it = warpedAudioClipCache.find(cacheKey); it != warpedAudioClipCache.end())
        return it->second;

    return {};
}

std::shared_ptr<const AudioEngine::AudioClipData> AudioEngine::getOrCreateWarpedAudioClip(const TrackPlaybackState::RegionPlaybackState& region,
                                                                                           double targetDurationSeconds)
{
    if (region.clipData == nullptr || targetDurationSeconds <= 0.0)
        return {};

    const auto sourceClipDurationSeconds = static_cast<double>(region.clipData->samples.getNumSamples()) / juce::jmax(1.0, region.clipData->sampleRate);
    const auto sourceStartSeconds = juce::jlimit(0.0, sourceClipDurationSeconds, region.sourceOffsetSeconds);
    const auto explicitDuration = region.sourceDurationSeconds > 0.0 ? region.sourceDurationSeconds : (sourceClipDurationSeconds - sourceStartSeconds);
    const auto sourceDurationSeconds = juce::jlimit(0.001, juce::jmax(0.001, sourceClipDurationSeconds - sourceStartSeconds), explicitDuration);

    const auto cacheKey = createWarpedAudioClipCacheKey(region.sourceFilePath, sourceStartSeconds, sourceDurationSeconds, targetDurationSeconds);

    if (const auto it = warpedAudioClipCache.find(cacheKey); it != warpedAudioClipCache.end())
        return it->second;

    auto warpedClip = std::make_shared<AudioClipData>();
    warpedClip->filePath = cacheKey;
    warpedClip->sampleRate = region.clipData->sampleRate;

    const auto targetSamples = juce::jmax(1, static_cast<int>(std::round(targetDurationSeconds * warpedClip->sampleRate)));
    warpedClip->samples.setSize(region.clipData->samples.getNumChannels(), targetSamples);

    const auto sourceStartSample = sourceStartSeconds * warpedClip->sampleRate;
    const auto sourceSpanSamples = sourceDurationSeconds * warpedClip->sampleRate;

    for (int channel = 0; channel < warpedClip->samples.getNumChannels(); ++channel)
    {
        const auto sourceChannel = juce::jmin(channel, region.clipData->samples.getNumChannels() - 1);
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto normalisedProgress = targetSamples > 1
                ? static_cast<double>(sample) / static_cast<double>(targetSamples - 1)
                : 0.0;
            const auto sourcePosition = sourceStartSample + (normalisedProgress * sourceSpanSamples);
            const auto sourceIndex = static_cast<int>(sourcePosition);
            const auto clampedIndex = juce::jlimit(0, region.clipData->samples.getNumSamples() - 1, sourceIndex);
            const auto nextIndex = juce::jlimit(0, region.clipData->samples.getNumSamples() - 1, clampedIndex + 1);
            const auto alpha = static_cast<float>(sourcePosition - static_cast<double>(sourceIndex));
            const auto sampleA = region.clipData->samples.getSample(sourceChannel, clampedIndex);
            const auto sampleB = region.clipData->samples.getSample(sourceChannel, nextIndex);
            warpedClip->samples.setSample(channel, sample, juce::jmap(alpha, sampleA, sampleB));
        }
    }

    warpedAudioClipCache[cacheKey] = warpedClip;
    return warpedClip;
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
        const auto blockTempo = juce::jmax(1.0,
                                           static_cast<double>(interpolateAutomationValue(track.tempoAutomation,
                                                                                          track.playheadBeat,
                                                                                          static_cast<float>(track.projectBpm))));
        const auto secondsPerBeat = 60.0 / blockTempo;
        for (const auto& region : track.regions)
        {
            if (region.kind != RegionKind::audio || region.clipData == nullptr || region.lengthInBeats <= 0.0)
                continue;

            const auto regionStartBeat = region.startBeat;
            const auto regionEndBeat = region.startBeat + region.lengthInBeats;
            const auto loopLengthBeats = juce::jlimit(0.001, region.lengthInBeats, region.loopLengthInBeats > 0.0 ? region.loopLengthInBeats : region.lengthInBeats);
            const auto blockStartBeat = track.playheadBeat;
            const auto blockEndBeat = blockStartBeat + (static_cast<double>(buffer.getNumSamples()) / currentSampleRate) / secondsPerBeat;

            if (blockEndBeat <= regionStartBeat || blockStartBeat >= regionEndBeat)
                continue;

            const auto regionStartProjectSeconds = projectSecondsForBeat(session, regionStartBeat);
            const auto loopEndProjectSeconds = projectSecondsForBeat(session, regionStartBeat + loopLengthBeats);
            const auto regionEndProjectSeconds = projectSecondsForBeat(session, regionEndBeat);
            const auto blockStartProjectSeconds = projectSecondsForBeat(session, blockStartBeat);
            const auto targetDurationSeconds = juce::jmax(0.001,
                                                          region.loopEnabled
                                                              ? (loopEndProjectSeconds - regionStartProjectSeconds)
                                                              : (regionEndProjectSeconds - regionStartProjectSeconds));
            const auto renderClip = region.warpEnabled ? findWarpedAudioClip(region, targetDurationSeconds) : region.clipData;

            if (renderClip == nullptr)
                continue;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto sampleBeat = blockStartBeat + (static_cast<double>(sample) / currentSampleRate) / secondsPerBeat;
                if (sampleBeat < regionStartBeat || sampleBeat >= regionEndBeat)
                    continue;

                const auto sampleProjectSeconds = blockStartProjectSeconds + (static_cast<double>(sample) / currentSampleRate);
                auto regionProjectOffsetSeconds = sampleProjectSeconds - regionStartProjectSeconds;
                if (region.loopEnabled)
                    regionProjectOffsetSeconds = std::fmod(juce::jmax(0.0, regionProjectOffsetSeconds), targetDurationSeconds);

                const auto sourceTimeSeconds = region.warpEnabled
                    ? juce::jlimit(0.0, targetDurationSeconds, regionProjectOffsetSeconds)
                    : region.sourceOffsetSeconds + regionProjectOffsetSeconds;
                const auto sourcePosition = sourceTimeSeconds * renderClip->sampleRate;
                const auto sampleIndex = static_cast<int>(sourcePosition);
                const auto alpha = static_cast<float>(sourcePosition - static_cast<double>(sampleIndex));
                const auto regionBeatOffset = sampleBeat - regionStartBeat;
                const auto distanceFromEndBeats = regionEndBeat - sampleBeat;
                const auto automatedVolume = interpolateAutomationValue(track.volumeAutomation, sampleBeat, track.volume);
                const auto automatedPan = interpolateAutomationValue(track.panAutomation, sampleBeat, track.pan);
                const auto leftGain = static_cast<float>(automatedVolume * bipolarPanLeftGain(automatedPan));
                const auto rightGain = static_cast<float>(automatedVolume * bipolarPanRightGain(automatedPan));

                if (sampleIndex < 0 || sampleIndex + 1 >= renderClip->samples.getNumSamples())
                    continue;

                const auto sampleLeftA = renderClip->samples.getSample(0, sampleIndex);
                const auto sampleLeftB = renderClip->samples.getSample(0, sampleIndex + 1);
                auto dryLeft = juce::jmap(alpha, sampleLeftA, sampleLeftB);

                float dryRight = dryLeft;
                if (renderClip->samples.getNumChannels() > 1)
                {
                    const auto sampleRightA = renderClip->samples.getSample(1, sampleIndex);
                    const auto sampleRightB = renderClip->samples.getSample(1, sampleIndex + 1);
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
                            if (auto* parameter = parameters[parameterIndex])
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

        const auto blockTempo = juce::jmax(1.0, track.bpm);
        const auto secondsPerBeat = 60.0 / blockTempo;
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
            const auto loopLengthBeats = juce::jlimit(0.001, region.lengthInBeats, region.loopLengthInBeats > 0.0 ? region.loopLengthInBeats : region.lengthInBeats);
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
                const auto loopBeatOffset = region.loopEnabled
                    ? std::fmod(juce::jmax(0.0, regionBeatOffset), loopLengthBeats)
                    : regionBeatOffset;

                auto mixedSample = 0.0f;

                for (const auto& note : region.midiNotes)
                {
                    const auto noteStartBeat = note.startBeat;
                    const auto noteEndBeat = note.startBeat + note.lengthInBeats;

                    if (loopBeatOffset < noteStartBeat || loopBeatOffset >= noteEndBeat)
                        continue;

                    const auto noteBeatOffset = loopBeatOffset - noteStartBeat;
                    const auto velocityGain = static_cast<float>(note.velocity / 127.0f);
                    const auto sampleProjectTempo = interpolateAutomationValue(track.tempoAutomation,
                                                                               sampleBeat,
                                                                               static_cast<float>(track.projectBpm));
                    const auto sampleTrackTempo = juce::jmax(1.0, static_cast<double>(sampleProjectTempo)
                                                                      * static_cast<double>(juce::jmax(0.25f, track.tempoMultiplier)));
                    const auto sampleSecondsPerBeat = 60.0 / sampleTrackTempo;
                    const auto envelope = midiRegionEnvelope(noteBeatOffset, note.lengthInBeats, sampleSecondsPerBeat);

                    mixedSample += midiRegionSample(track.kind, note.pitch, noteBeatOffset, sampleSecondsPerBeat)
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
                const auto repetitionCount = region.loopEnabled
                    ? juce::jmax(1, static_cast<int>(std::ceil(region.lengthInBeats / loopLengthBeats)))
                    : 1;

                for (int repetition = 0; repetition < repetitionCount; ++repetition)
                {
                    const auto repetitionStartBeat = region.startBeat + static_cast<double>(repetition) * loopLengthBeats;
                    const auto repetitionEndBeat = juce::jmin(regionEndBeat, repetitionStartBeat + loopLengthBeats);
                    if (repetitionStartBeat >= regionEndBeat)
                        break;

                    for (const auto& note : region.midiNotes)
                    {
                        const auto noteStartBeat = repetitionStartBeat + note.startBeat;
                        const auto noteEndBeat = juce::jmin(repetitionEndBeat, noteStartBeat + note.lengthInBeats);
                        if (noteStartBeat >= repetitionEndBeat)
                            continue;

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
                            if (auto* parameter = instrumentParameters[parameterIndex])
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
                                if (auto* parameter = parameters[parameterIndex])
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

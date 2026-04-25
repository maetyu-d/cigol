#pragma once

#include "../Core/SessionModel.h"
#include "SuperColliderBridge.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <thread>
#include <set>

namespace cigol
{
class AudioEngine final : public juce::AudioIODeviceCallback,
                          private juce::Timer
{
public:
    struct LoadablePluginChoice
    {
        juce::String name;
        juce::String identifier;
        bool isInstrument { false };
    };

    struct RememberedPluginEntry
    {
        juce::String name;
        juce::String identifier;
        bool isInstrument { false };
        bool enabled { true };
    };

    struct HostedInsertMeterState
    {
        int trackId { -1 };
        int slotIndex { -1 };
        float inputLevel { 0.0f };
        float outputLevel { 0.0f };
        bool active { false };
    };

    struct AutomatableParameterChoice
    {
        int slotIndex { -1 };
        int parameterIndex { -1 };
        juce::String name;
        float currentNormalisedValue { 0.0f };
    };

    struct PluginParameterBindingStatus
    {
        bool slotAvailable { false };
        bool parameterAvailable { false };
        juce::String resolvedName;
    };

    struct OfflineRenderRequest
    {
        double startBeat { 1.0 };
        double endBeat { 1.0 };
        std::optional<int> trackId;
        bool normaliseOutput { false };
        double tailSeconds { 0.0 };
    };

    AudioEngine(SessionState& sessionToUse, SuperColliderBridge& bridgeToUse);
    ~AudioEngine() override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;

    juce::String getEngineSummary() const;
    juce::String getPluginHostingSummary() const;
    std::vector<LoadablePluginChoice> getAvailablePluginChoices(const TrackState& track, int slotIndex);
    std::vector<RememberedPluginEntry> getRememberedPluginCatalog() const;
    juce::String getPluginScanStatus() const;
    bool isPluginScanInProgress() const;
    void requestPluginRescan();
    bool setPluginEnabled(const juce::String& pluginIdentifier, bool enabled);
    std::vector<AutomatableParameterChoice> getAutomatableParameters(int trackId) const;
    PluginParameterBindingStatus getTrackPluginParameterBindingStatus(int trackId, int slotIndex, int parameterIndex) const;
    float getTrackPluginParameterValue(int trackId, int slotIndex, int parameterIndex) const;
    bool setTrackPluginParameterValue(int trackId, int slotIndex, int parameterIndex, float normalisedValue);
    std::optional<HostedInsertMeterState> getHostedInsertMeterState(int trackId, int slotIndex) const;
    bool setTrackSlotBypassed(int trackId, int slotIndex, bool bypassed);
    bool setTrackSuperColliderInsertMix(int trackId, int slotIndex, float wetMix, float outputTrimDb);
    bool setTrackSlotPlugin(int trackId, int slotIndex, const juce::String& pluginIdentifier);
    void clearTrackSlotPlugin(int trackId, int slotIndex);
    std::unique_ptr<juce::AudioProcessorEditor> createTrackSlotEditor(int trackId, int slotIndex);
    void syncPluginStatesToSession();
    void reloadSessionState();
    bool renderOfflineToFile(const OfflineRenderRequest& request, const juce::File& targetFile, juce::String& message);
    bool isWarpedAudioClipReady(const Region& region, double targetDurationSeconds);
    void prepareWarpedAudioClip(const Region& region, double targetDurationSeconds);

    struct AudioClipData
    {
        juce::String filePath;
        double sampleRate { 44100.0 };
        juce::AudioBuffer<float> samples;
    };

    struct TrackPlaybackState
    {
        struct InsertPlaybackState
        {
            int slotIndex { -1 };
            ProcessorKind kind { ProcessorKind::audioUnit };
            bool bypassed { false };
            bool hasSuperColliderState { false };
            float wetMix { 1.0f };
            float outputTrimDb { 0.0f };
        };

        struct RegionPlaybackState
        {
            struct MidiNotePlaybackState
            {
                int pitch { 60 };
                double startBeat { 0.0 };
                double lengthInBeats { 1.0 };
                uint8_t velocity { 100 };
            };

            RegionKind kind { RegionKind::audio };
            double startBeat { 0.0 };
            double lengthInBeats { 0.0 };
            double sourceOffsetSeconds { 0.0 };
            double fadeInBeats { 0.0 };
            double fadeOutBeats { 0.0 };
            float gain { 1.0f };
            bool warpEnabled { false };
            double sourceDurationSeconds { 0.0 };
            bool loopEnabled { false };
            double loopLengthInBeats { 0.0 };
            juce::String sourceFilePath;
            std::shared_ptr<const struct AudioClipData> clipData;
            std::vector<MidiNotePlaybackState> midiNotes;
        };

        int trackId { 0 };
        TrackKind kind { TrackKind::audio };
        TrackChannelMode channelMode { TrackChannelMode::stereo };
        bool transportPlaying { false };
        bool muted { false };
        bool hasSuperColliderMidiGenerator { false };
        bool hasActiveSuperColliderFx { false };
        double bpm { 120.0 };
        double projectBpm { 120.0 };
        double playheadBeat { 1.0 };
        float tempoMultiplier { 1.0f };
        float volume { 0.0f };
        float pan { 0.0f };
        std::vector<AutomationPoint> tempoAutomation;
        std::vector<AutomationPoint> volumeAutomation;
        std::vector<AutomationPoint> panAutomation;
        std::vector<PluginAutomationLane> pluginAutomationLanes;
        int selectedPluginAutomationLaneIndex { -1 };
        std::vector<InsertPlaybackState> inserts;
        std::vector<RegionPlaybackState> regions;
    };

private:
    struct CachedPluginDescription
    {
        juce::PluginDescription description;
        bool enabled { true };
    };

    struct HostedPluginRuntime
    {
        int slotIndex { -1 };
        juce::String identifier;
        bool isInstrument { false };
        bool bypassed { false };
        std::unique_ptr<juce::AudioPluginInstance> instance;
    };

    struct HostedTrackRuntime
    {
        int trackId { 0 };
        std::vector<HostedPluginRuntime> slots;
    };

    void renderAudioRegions(juce::AudioBuffer<float>& buffer);
    void renderMidiRegions(juce::AudioBuffer<float>& buffer);
    void timerCallback() override;
    void rebuildGraph(double sampleRate, int blockSize);
    void syncTrackPlaybackStates();
    void refreshHostedPlugins();
    void loadPluginCatalogCache();
    void savePluginCatalogCache() const;
    juce::File getPluginCatalogCacheFile() const;
    void joinCompletedPluginScanThread();
    void commitPendingPluginScanResults();
    juce::PluginDescription findPluginDescription(const juce::String& identifier) const;
    juce::PluginDescription findSuperColliderBridgeDescription() const;
    std::shared_ptr<const AudioClipData> getOrLoadAudioClip(const juce::String& filePath);
    juce::String createWarpedAudioClipCacheKey(const juce::String& filePath,
                                               double sourceStartSeconds,
                                               double sourceDurationSeconds,
                                               double targetDurationSeconds) const;
    std::shared_ptr<const AudioClipData> findWarpedAudioClip(const TrackPlaybackState::RegionPlaybackState& region,
                                                             double targetDurationSeconds) const;
    std::shared_ptr<const AudioClipData> getOrCreateWarpedAudioClip(const TrackPlaybackState::RegionPlaybackState& region,
                                                                    double targetDurationSeconds);
    void updateMeters();

    SessionState& session;
    SuperColliderBridge& superColliderBridge;

    juce::AudioFormatManager audioFormatManager;
    juce::AudioPluginFormatManager pluginFormatManager;
    juce::AudioProcessorGraph graph;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> graphBuffer;
    std::vector<juce::AudioProcessorGraph::NodeID> trackNodeIds;
    std::vector<TrackPlaybackState> trackPlaybackStates;
    std::vector<HostedTrackRuntime> hostedTrackRuntimes;
    std::map<juce::String, std::shared_ptr<const AudioClipData>> audioClipCache;
    std::map<juce::String, std::shared_ptr<const AudioClipData>> warpedAudioClipCache;
    std::map<int, std::set<int>> activeMidiNotesByTrack;
    std::vector<HostedInsertMeterState> hostedInsertMeterStates;
    juce::SpinLock trackStateLock;
    juce::SpinLock pluginRuntimeLock;
    juce::SpinLock hostedInsertMeterLock;
    juce::CriticalSection pluginCatalogLock;
    juce::AudioProcessorGraph::NodeID audioInputNodeId {};
    juce::AudioProcessorGraph::NodeID audioOutputNodeId {};
    double currentSampleRate { 44100.0 };
    int currentBlockSize { 512 };
    std::vector<CachedPluginDescription> cachedPluginDescriptions;
    std::vector<CachedPluginDescription> pendingCachedPluginDescriptions;
    juce::String pluginScanStatus { "Plugin library idle" };
    juce::String pendingPluginScanStatus;
    std::thread pluginScanThread;
    std::atomic<bool> pluginScanInProgress { false };
    std::atomic<bool> pluginScanResultsPending { false };
    bool deferredPluginInitialisationPending { true };
};
} // namespace cigol

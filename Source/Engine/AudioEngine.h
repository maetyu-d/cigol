#pragma once

#include "../Core/SessionModel.h"
#include "SuperColliderBridge.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <set>

namespace logiclikedaw
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
    bool setTrackSlotPlugin(int trackId, int slotIndex, const juce::String& pluginIdentifier);
    void clearTrackSlotPlugin(int trackId, int slotIndex);
    std::unique_ptr<juce::AudioProcessorEditor> createTrackSlotEditor(int trackId, int slotIndex);
    void reloadSessionState();

    struct AudioClipData
    {
        juce::String filePath;
        double sampleRate { 44100.0 };
        juce::AudioBuffer<float> samples;
    };

    struct TrackPlaybackState
    {
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
            juce::String sourceFilePath;
            std::shared_ptr<const struct AudioClipData> clipData;
            std::vector<MidiNotePlaybackState> midiNotes;
        };

        int trackId { 0 };
        TrackKind kind { TrackKind::audio };
        bool transportPlaying { false };
        bool muted { false };
        bool hasSuperColliderMidiGenerator { false };
        bool hasActiveSuperColliderFx { false };
        double bpm { 120.0 };
        double playheadBeat { 1.0 };
        float volume { 0.0f };
        float pan { 0.0f };
        std::vector<AutomationPoint> volumeAutomation;
        std::vector<AutomationPoint> panAutomation;
        std::vector<RegionPlaybackState> regions;
    };

private:
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
    void scanForPlugins();
    juce::PluginDescription findPluginDescription(const juce::String& identifier) const;
    juce::PluginDescription findSuperColliderBridgeDescription() const;
    std::shared_ptr<const AudioClipData> getOrLoadAudioClip(const juce::String& filePath);
    void updateMeters();

    SessionState& session;
    SuperColliderBridge& superColliderBridge;

    juce::AudioFormatManager audioFormatManager;
    juce::AudioPluginFormatManager pluginFormatManager;
    juce::KnownPluginList knownPluginList;
    juce::AudioProcessorGraph graph;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> graphBuffer;
    std::vector<juce::AudioProcessorGraph::NodeID> trackNodeIds;
    std::vector<TrackPlaybackState> trackPlaybackStates;
    std::vector<HostedTrackRuntime> hostedTrackRuntimes;
    std::map<juce::String, std::shared_ptr<const AudioClipData>> audioClipCache;
    std::map<int, std::set<int>> activeMidiNotesByTrack;
    juce::SpinLock trackStateLock;
    juce::SpinLock pluginRuntimeLock;
    juce::AudioProcessorGraph::NodeID audioInputNodeId {};
    juce::AudioProcessorGraph::NodeID audioOutputNodeId {};
    double currentSampleRate { 44100.0 };
    int currentBlockSize { 512 };
    bool pluginsScanned { false };
};
} // namespace logiclikedaw

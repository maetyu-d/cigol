#pragma once

#include "../Core/SessionModel.h"

#include <map>
#include <set>

#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>

namespace logiclikedaw
{
struct SuperColliderTrackSnapshot
{
    int trackId {};
    juce::String trackName;
    TrackKind trackKind { TrackKind::audio };
    juce::String routingSummary;
    bool hasRenderScript { false };
    bool hasFxInsert { false };
    bool hasMidiGenerator { false };
};

struct SynthDefDescriptor
{
    juce::String name;
    juce::String type;
    juce::String description;
    juce::StringArray parameters;
};

struct SuperColliderRuntimeState
{
    bool appBundleDetected { false };
    bool scsynthDetected { false };
    bool sclangDetected { false };
    bool scsynthRunning { false };
    bool sclangRunning { false };
    bool sclangUsable { false };
    bool oscConnected { false };
    bool transportMirroredToServer { false };
    int serverPort { 57110 };
    int clientPort { 57120 };
    int rootGroupId { 1000 };
    int transportControlBus { 32 };
    int tempoControlBus { 33 };
    int playheadControlBus { 34 };
    int activeRenderNodeCount { 0 };
    int activeFxNodeCount { 0 };
    int activeMidiGeneratorCount { 0 };
    int allocatedAudioBusCount { 0 };
    int allocatedControlBusCount { 0 };
    int loadedSynthDefCount { 0 };
    int catalogedSynthDefCount { 0 };
    int sourceSynthDefCount { 0 };
    int autoCompiledSynthDefCount { 0 };
    juce::String appBundlePath;
    juce::String scsynthPath;
    juce::String sclangPath;
    juce::String synthDefDirectoryPath;
    juce::String statusLine;
    juce::String lastOscAction;
    juce::String diagnostics;
};

class SuperColliderBridge
{
public:
    virtual ~SuperColliderBridge() = default;

    virtual void refreshEnvironment(SessionState& session) = 0;
    virtual bool ensureServerRunning(SessionState& session) = 0;
    virtual bool rebuildSynthDefs(SessionState& session) = 0;
    virtual void poll(SessionState& session) = 0;
    virtual void shutdown(SessionState& session) = 0;

    virtual const SuperColliderRuntimeState& getRuntimeState() const = 0;

    virtual juce::String getConnectionSummary(const SessionState& session) const = 0;
    virtual juce::String describeTrack(const TrackState& track) const = 0;
    virtual std::vector<SuperColliderTrackSnapshot> createSnapshots(const SessionState& session) const = 0;
};

class SuperColliderProcessBridge final : public SuperColliderBridge
{
public:
    SuperColliderProcessBridge();
    ~SuperColliderProcessBridge() override;

    void refreshEnvironment(SessionState& session) override;
    bool ensureServerRunning(SessionState& session) override;
    bool rebuildSynthDefs(SessionState& session) override;
    void poll(SessionState& session) override;
    void shutdown(SessionState& session) override;

    const SuperColliderRuntimeState& getRuntimeState() const override;

    juce::String getConnectionSummary(const SessionState& session) const override;
    juce::String describeTrack(const TrackState& track) const override;
    std::vector<SuperColliderTrackSnapshot> createSnapshots(const SessionState& session) const override;

private:
    bool connectOsc();
    void initialiseSessionGraph(const SessionState& session);
    void compileSynthDefSources();
    void loadSynthDefs(const SessionState& session);
    void loadSynthDefCatalog();
    void allocateTrackBuses(const SessionState& session);
    void syncTransportState(const SessionState& session);
    void syncGlobalTransportControls(const SessionState& session);
    void createRenderNode(const TrackState& track);
    void updateRenderNode(const TrackState& track);
    void syncFxState(const SessionState& session);
    void createFxNode(const TrackState& track, const TrackProcessorState& insert);
    void updateFxNode(const TrackState& track, const TrackProcessorState& insert);
    void freeFxNode(int trackId);
    void freeAllFxNodes();
    void syncMidiGeneratorState(const SessionState& session);
    void createMidiGeneratorProxy(const TrackState& track);
    void updateMidiGeneratorProxy(const TrackState& track, double phaseBeat);
    void freeMidiGeneratorProxy(int trackId);
    void freeAllMidiGeneratorProxies();
    void freeRenderNode(int trackId);
    void freeAllRenderNodes();
    int synthNodeIdForTrack(const TrackState& track) const;
    int fxNodeIdForTrack(const TrackState& track) const;
    int midiControlBusForTrack(const TrackState& track) const;
    int renderAudioBusForTrack(const TrackState& track) const;
    int fxAudioBusForTrack(const TrackState& track) const;
    juce::File locateSynthDefDirectory() const;
    juce::String escapeForScString(const juce::String& text) const;
    bool runSclangScript(const juce::String& scriptBody, int timeoutMs, juce::String& output) const;
    juce::String resolveRenderSynthDefName(const TrackState& track) const;
    juce::String resolveFxSynthDefName(const TrackState& track, const TrackProcessorState& insert) const;
    bool isSynthDefAvailable(const juce::String& synthDefName) const;
    const SynthDefDescriptor* findSynthDefDescriptor(const juce::String& synthDefName) const;
    float frequencyForRenderTrack(const TrackState& track) const;
    float frequencyForFxTrack(const TrackState& track) const;
    int groupIdForTrack(const TrackState& track) const;
    void discoverInstallation();
    void probeLanguageBinary();
    void syncSession(SessionState& session);
    void updateProcessFlags();

    SuperColliderRuntimeState runtimeState;
    std::unique_ptr<juce::ChildProcess> scsynthProcess;
    juce::OSCSender oscSender;
    std::map<int, int> activeRenderNodes;
    std::map<int, int> activeFxNodes;
    std::map<int, bool> activeMidiGeneratorTracks;
    std::map<int, int> renderAudioBuses;
    std::map<int, int> fxAudioBuses;
    std::map<int, int> midiControlBuses;
    std::set<juce::String> availableSynthDefs;
    std::map<juce::String, SynthDefDescriptor> synthDefCatalog;
    bool lastTransportPlaying { false };
};
} // namespace logiclikedaw

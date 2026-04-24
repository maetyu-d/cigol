#include "SuperColliderBridge.h"

#include <algorithm>
#include <cmath>

namespace cigol
{
namespace
{
constexpr auto sclangProbeMarker = "CIGOL_SCLANG_OK";
constexpr auto synthDefCompileMarker = "CIGOL_SYNTHDEF_OK";

juce::File findSuperColliderApp()
{
    const juce::File app("/Applications/SuperCollider.app");
    return app.exists() ? app : juce::File();
}

juce::String describeProcessIssue(const juce::String& output)
{
    if (output.containsIgnoreCase("Incompatible processor"))
        return "sclang was found, but this launch path hit a Qt processor compatibility error.";

    if (output.isNotEmpty())
        return output.upToFirstOccurrenceOf("\n", false, false).trim();

    return {};
}
} // namespace

SuperColliderProcessBridge::SuperColliderProcessBridge()
{
    discoverInstallation();
}

SuperColliderProcessBridge::~SuperColliderProcessBridge()
{
    SessionState scratchSession;
    shutdown(scratchSession);
}

void SuperColliderProcessBridge::refreshEnvironment(SessionState& session)
{
    discoverInstallation();
    probeLanguageBinary();
    compileSynthDefSources();
    loadSynthDefCatalog();
    updateProcessFlags();
    syncSession(session);
}

bool SuperColliderProcessBridge::ensureServerRunning(SessionState& session)
{
    refreshEnvironment(session);

    if (runtimeState.scsynthRunning)
        return true;

    if (! runtimeState.scsynthDetected)
    {
        runtimeState.statusLine = "scsynth not found";
        syncSession(session);
        return false;
    }

    auto process = std::make_unique<juce::ChildProcess>();
    const juce::StringArray command {
        runtimeState.scsynthPath,
        "-u", juce::String(runtimeState.serverPort),
        "-B", "127.0.0.1",
        "-R", "0",
        "-l", "8",
        "-i", "2",
        "-o", "2"
    };

    if (! process->start(command))
    {
        runtimeState.statusLine = "Failed to launch scsynth";
        runtimeState.diagnostics = "Could not start " + runtimeState.scsynthPath;
        syncSession(session);
        return false;
    }

    scsynthProcess = std::move(process);
    juce::Thread::sleep(150);
    updateProcessFlags();

    runtimeState.statusLine = runtimeState.scsynthRunning
        ? "scsynth running on UDP " + juce::String(runtimeState.serverPort)
        : "scsynth exited during launch";

    if (scsynthProcess != nullptr && ! runtimeState.scsynthRunning)
        runtimeState.diagnostics = scsynthProcess->readAllProcessOutput().trim();

    if (runtimeState.scsynthRunning && connectOsc())
    {
        loadSynthDefs(session);
        allocateTrackBuses(session);
        initialiseSessionGraph(session);
    }

    syncSession(session);
    return runtimeState.scsynthRunning;
}

bool SuperColliderProcessBridge::rebuildSynthDefs(SessionState& session)
{
    discoverInstallation();
    probeLanguageBinary();

    runtimeState.lastOscAction = "Rebuilding SynthDefs from source";
    runtimeState.diagnostics = "Scanning manifest and refreshing local synthdefs.";

    compileSynthDefSources();
    loadSynthDefCatalog();

    const auto hadActiveServer = runtimeState.scsynthRunning && runtimeState.oscConnected;

    if (hadActiveServer)
    {
        freeAllRenderNodes();
        freeAllFxNodes();
        freeAllMidiGeneratorProxies();
    }

    loadSynthDefs(session);
    allocateTrackBuses(session);

    if (hadActiveServer)
    {
        if (session.transport.playing)
            syncTransportState(session);

        runtimeState.lastOscAction = runtimeState.loadedSynthDefCount > 0
            ? "Reloaded SynthDefs into the running SuperCollider server"
            : "SuperCollider server refreshed with default synth fallback";
    }
    else if (runtimeState.loadedSynthDefCount > 0)
    {
        runtimeState.lastOscAction = "Rebuilt SynthDefs for the next server session";
    }
    else if (! runtimeState.sclangUsable)
    {
        runtimeState.lastOscAction = "SynthDef rebuild unavailable";
    }
    else
    {
        runtimeState.lastOscAction = "SynthDefs refreshed";
    }

    syncSession(session);
    return runtimeState.loadedSynthDefCount > 0 || runtimeState.autoCompiledSynthDefCount > 0;
}

void SuperColliderProcessBridge::poll(SessionState& session)
{
    updateProcessFlags();

    if (runtimeState.scsynthRunning && runtimeState.oscConnected)
    {
        syncTransportState(session);
        oscSender.send("/status");
        if (runtimeState.lastOscAction.isEmpty())
            runtimeState.lastOscAction = "Sent /status ping";
    }

    syncSession(session);
}

void SuperColliderProcessBridge::shutdown(SessionState& session)
{
    if (runtimeState.oscConnected)
    {
        freeAllRenderNodes();
        freeAllFxNodes();
        freeAllMidiGeneratorProxies();
        oscSender.send("/clearSched");
        oscSender.send("/g_freeAll", runtimeState.rootGroupId);
        oscSender.disconnect();
    }

    if (scsynthProcess != nullptr)
    {
        scsynthProcess->kill();
        scsynthProcess.reset();
    }

    updateProcessFlags();
    runtimeState.oscConnected = false;
    runtimeState.transportMirroredToServer = false;
    runtimeState.statusLine = runtimeState.scsynthDetected
        ? "SuperCollider server stopped"
        : "SuperCollider unavailable";
    syncSession(session);
}

const SuperColliderRuntimeState& SuperColliderProcessBridge::getRuntimeState() const
{
    return runtimeState;
}

juce::String SuperColliderProcessBridge::getConnectionSummary(const SessionState& session) const
{
    juce::ignoreUnused(session);

    auto summary = runtimeState.statusLine.isNotEmpty() ? runtimeState.statusLine : "SuperCollider not configured";

    if (runtimeState.scsynthRunning)
        summary += " | Render: UDP " + juce::String(runtimeState.serverPort);

    if (runtimeState.oscConnected)
        summary += " | OSC live";

    if (runtimeState.allocatedAudioBusCount > 0)
        summary += " | audio buses " + juce::String(runtimeState.allocatedAudioBusCount);

    if (runtimeState.allocatedControlBusCount > 0)
        summary += " | control buses " + juce::String(runtimeState.allocatedControlBusCount);

    if (runtimeState.loadedSynthDefCount > 0)
        summary += " | synthdefs " + juce::String(runtimeState.loadedSynthDefCount);

    if (runtimeState.catalogedSynthDefCount > 0)
        summary += " | catalog " + juce::String(runtimeState.catalogedSynthDefCount);

    if (runtimeState.sourceSynthDefCount > 0)
        summary += " | sources " + juce::String(runtimeState.sourceSynthDefCount);

    if (runtimeState.autoCompiledSynthDefCount > 0)
        summary += " | auto-compiled " + juce::String(runtimeState.autoCompiledSynthDefCount);

    if (runtimeState.activeRenderNodeCount > 0)
        summary += " | render nodes " + juce::String(runtimeState.activeRenderNodeCount);

    if (runtimeState.activeFxNodeCount > 0)
        summary += " | fx nodes " + juce::String(runtimeState.activeFxNodeCount);

    if (runtimeState.activeMidiGeneratorCount > 0)
        summary += " | midi mirrors " + juce::String(runtimeState.activeMidiGeneratorCount);

    if (runtimeState.sclangDetected && ! runtimeState.sclangUsable)
        summary += " | sclang blocked";

    return summary;
}

juce::String SuperColliderProcessBridge::describeTrack(const TrackState& track) const
{
    if (track.kind == TrackKind::superColliderRender && track.renderScript.has_value())
    {
        const auto synthDefName = resolveRenderSynthDefName(track);
        auto description = "SC render track via " + track.renderScript->entryNode + " | synthdef " + synthDefName + " | bus " + juce::String(renderAudioBusForTrack(track));

        if (const auto* descriptor = findSynthDefDescriptor(synthDefName))
            description += " | " + descriptor->description;

        return description;
    }

    if (track.midiGenerator.kind == MidiGeneratorKind::superCollider && track.midiGenerator.superCollider.has_value())
    {
        const auto suffix = activeMidiGeneratorTracks.contains(track.id)
            ? "control bus live"
            : (runtimeState.sclangUsable ? "language bridge ready" : "control mirror pending");
        return "SC MIDI generator via " + track.midiGenerator.superCollider->entryNode + " | bus " + juce::String(midiControlBusForTrack(track)) + " | " + suffix;
    }

    for (const auto& insert : track.inserts)
    {
        if (insert.kind == ProcessorKind::superColliderFx && insert.superCollider.has_value())
        {
            const auto synthDefName = resolveFxSynthDefName(track, insert);
            juce::String suffix;
            if (const auto hostedIt = hostedInsertRouting.find(track.id); hostedIt != hostedInsertRouting.end() && hostedIt->second.active)
            {
                suffix = "host insert live in " + juce::String(hostedIt->second.inputLevel, 2)
                    + " out " + juce::String(hostedIt->second.outputLevel, 2);
            }
            else
            {
                suffix = activeFxNodes.contains(track.id)
                    ? "fx proxy live"
                    : (runtimeState.scsynthRunning ? "server online" : "server not booted");
            }

            auto description = "SC insert " + insert.name + " | synthdef " + synthDefName + " | audio bus " + juce::String(fxAudioBusForTrack(track)) + " | " + suffix;

            if (const auto* descriptor = findSynthDefDescriptor(synthDefName))
                description += " | " + descriptor->description;

            return description;
        }
    }

    return "No active SuperCollider routing";
}

std::vector<SuperColliderTrackSnapshot> SuperColliderProcessBridge::createSnapshots(const SessionState& session) const
{
    std::vector<SuperColliderTrackSnapshot> snapshots;

    for (const auto& track : session.tracks)
    {
        bool hasFxInsert = false;

        for (const auto& insert : track.inserts)
            hasFxInsert = hasFxInsert || insert.kind == ProcessorKind::superColliderFx;

        snapshots.push_back({
            track.id,
            track.name,
            track.kind,
            describeTrack(track),
            track.renderScript.has_value(),
            hasFxInsert,
            track.midiGenerator.kind == MidiGeneratorKind::superCollider
        });
    }

    return snapshots;
}

void SuperColliderProcessBridge::updateHostedInsertRouting(const SessionState& session,
                                                           const std::vector<HostedInsertRoutingSnapshot>& snapshots)
{
    hostedInsertRouting.clear();

    for (const auto& snapshot : snapshots)
    {
        if (snapshot.trackId < 0)
            continue;

        hostedInsertRouting[snapshot.trackId] = snapshot;

        if (! runtimeState.oscConnected || ! snapshot.active)
            continue;

        const auto trackIt = std::find_if(session.tracks.begin(), session.tracks.end(), [&snapshot] (const auto& track) {
            return track.id == snapshot.trackId;
        });

        if (trackIt == session.tracks.end())
            continue;

        oscSender.send("/n_set", fxNodeIdForTrack(*trackIt),
                       juce::String("amp"), juce::jlimit(0.0f, 1.0f, snapshot.outputLevel));
    }
}

void SuperColliderProcessBridge::discoverInstallation()
{
    const auto appBundle = findSuperColliderApp();
    runtimeState.appBundleDetected = appBundle.exists();
    runtimeState.appBundlePath = appBundle.getFullPathName();

    const auto scsynthFile = appBundle.getChildFile("Contents/Resources/scsynth");
    runtimeState.scsynthDetected = scsynthFile.existsAsFile();
    runtimeState.scsynthPath = scsynthFile.getFullPathName();

    const auto sclangFile = appBundle.getChildFile("Contents/MacOS/sclang");
    runtimeState.sclangDetected = sclangFile.existsAsFile();
    runtimeState.sclangPath = sclangFile.getFullPathName();

    if (! runtimeState.appBundleDetected)
        runtimeState.statusLine = "SuperCollider.app not found";
    else if (! runtimeState.scsynthDetected)
        runtimeState.statusLine = "scsynth missing from SuperCollider.app";
    else if (! runtimeState.scsynthRunning)
        runtimeState.statusLine = "SuperCollider binaries discovered";
}

void SuperColliderProcessBridge::probeLanguageBinary()
{
    runtimeState.sclangUsable = false;

    if (! runtimeState.sclangDetected)
        return;

    juce::String output;
    const auto script = juce::String()
        + "\"" + juce::String(sclangProbeMarker) + "\".postln;\n"
        + "0.exit;\n";

    const auto launched = runSclangScript(script, 4000, output);
    runtimeState.sclangUsable = launched && output.contains(sclangProbeMarker);

    if (! runtimeState.sclangUsable)
        runtimeState.diagnostics = launched ? describeProcessIssue(output) : "Could not execute sclang";
}

void SuperColliderProcessBridge::syncSession(SessionState& session)
{
    session.routing.superColliderServerConnected = runtimeState.scsynthRunning;
    session.routing.renderBusName = "scsynth:" + juce::String(runtimeState.serverPort) + (runtimeState.oscConnected ? " / OSC" : "");
    session.routing.fxBusName = runtimeState.scsynthRunning ? "SC FX via running server graph" : "SC FX server offline";
    session.routing.midiBusName = runtimeState.sclangUsable ? "sclang MIDI bridge ready" : "sclang unavailable";
}

void SuperColliderProcessBridge::updateProcessFlags()
{
    runtimeState.scsynthRunning = scsynthProcess != nullptr && scsynthProcess->isRunning();
    runtimeState.sclangRunning = false;

    if (! runtimeState.scsynthRunning)
        runtimeState.oscConnected = false;
}

bool SuperColliderProcessBridge::connectOsc()
{
    if (runtimeState.oscConnected)
        return true;

    runtimeState.oscConnected = oscSender.connect("127.0.0.1", runtimeState.serverPort);

    if (runtimeState.oscConnected)
    {
        runtimeState.lastOscAction = "Connected OSC sender to scsynth";
        runtimeState.diagnostics = {};
    }
    else
    {
        runtimeState.diagnostics = "Could not connect OSC sender to scsynth";
    }

    return runtimeState.oscConnected;
}

void SuperColliderProcessBridge::initialiseSessionGraph(const SessionState& session)
{
    if (! runtimeState.oscConnected)
        return;

    oscSender.send("/notify", 1);
    oscSender.send("/clearSched");
    oscSender.send("/g_new", runtimeState.rootGroupId, 0, 0);

    int configuredGroups = 0;

    for (const auto& track : session.tracks)
    {
        const bool needsScGroup = track.kind == TrackKind::superColliderRender
            || track.midiGenerator.kind == MidiGeneratorKind::superCollider
            || std::any_of(track.inserts.begin(), track.inserts.end(), [] (const auto& insert) {
                   return insert.kind == ProcessorKind::superColliderFx;
               });

        if (! needsScGroup)
            continue;

        oscSender.send("/g_new", groupIdForTrack(track), 1, runtimeState.rootGroupId);
        ++configuredGroups;
    }

    runtimeState.lastOscAction = "Initialised SC session graph with " + juce::String(configuredGroups) + " track groups";
}

void SuperColliderProcessBridge::loadSynthDefs(const SessionState& session)
{
    juce::ignoreUnused(session);

    availableSynthDefs.clear();
    runtimeState.loadedSynthDefCount = 0;

    const auto synthDefDir = locateSynthDefDirectory();
    runtimeState.synthDefDirectoryPath = synthDefDir.getFullPathName();

    if (! synthDefDir.isDirectory())
    {
        runtimeState.diagnostics = "No SynthDefs directory found. Falling back to built-in default synths.";
        return;
    }

    const auto synthDefFiles = synthDefDir.findChildFiles(juce::File::findFiles, false, "*.scsyndef");

    if (synthDefFiles.isEmpty())
    {
        runtimeState.diagnostics = "SynthDefs directory found but contains no .scsyndef files.";
        return;
    }

    for (const auto& file : synthDefFiles)
        availableSynthDefs.insert(file.getFileNameWithoutExtension());

    runtimeState.loadedSynthDefCount = static_cast<int>(availableSynthDefs.size());

    if (runtimeState.oscConnected)
        oscSender.send("/d_loadDir", synthDefDir.getFullPathName());

    runtimeState.lastOscAction = "Loaded synthdefs from " + synthDefDir.getFileName();
    runtimeState.diagnostics = {};
}

void SuperColliderProcessBridge::compileSynthDefSources()
{
    runtimeState.sourceSynthDefCount = 0;
    runtimeState.autoCompiledSynthDefCount = 0;

    if (! runtimeState.sclangUsable)
        return;

    const auto synthDefDir = locateSynthDefDirectory();
    const auto manifestFile = synthDefDir.getChildFile("manifest.tsv");

    if (! manifestFile.existsAsFile())
        return;

    juce::StringArray lines;
    lines.addLines(manifestFile.loadFileAsString());

    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();

        if (line.isEmpty() || line.startsWithChar('#'))
            continue;

        juce::StringArray columns;
        columns.addTokens(line, "\t", {});

        if (columns.size() < 4)
            continue;

        const auto name = columns[0].trim();
        const auto source = synthDefDir.getChildFile(columns[2].trim());
        const auto output = synthDefDir.getChildFile(columns[3].trim());

        ++runtimeState.sourceSynthDefCount;

        if (! source.existsAsFile())
            continue;

        if (output.existsAsFile() && output.getLastModificationTime() >= source.getLastModificationTime())
            continue;

        juce::String outputLog;
        const auto script = juce::String()
            + "~logicLikeSynthDef = nil;\n"
            + "thisProcess.interpreter.executeFile(\"" + escapeForScString(source.getFullPathName()) + "\");\n"
            + "if (~logicLikeSynthDef.isNil) {\n"
            + "    \"Missing ~logicLikeSynthDef in " + escapeForScString(source.getFileName()) + "\".postln;\n"
            + "    1.exit;\n"
            + "};\n"
            + "~logicLikeSynthDef.writeDefFile(\"" + escapeForScString(synthDefDir.getFullPathName()) + "\");\n"
            + "\"" + juce::String(synthDefCompileMarker) + ":" + escapeForScString(name) + "\".postln;\n"
            + "0.exit;\n";

        const auto compileMarker = juce::String(synthDefCompileMarker) + ":" + name;
        const auto compiled = runSclangScript(script, 8000, outputLog)
            && outputLog.contains(compileMarker)
            && output.existsAsFile();

        if (compiled)
        {
            ++runtimeState.autoCompiledSynthDefCount;
            runtimeState.diagnostics = "Auto-compiled synthdefs from source";
        }
        else
        {
            runtimeState.diagnostics = describeProcessIssue(outputLog);
        }
    }
}

void SuperColliderProcessBridge::loadSynthDefCatalog()
{
    synthDefCatalog.clear();
    runtimeState.catalogedSynthDefCount = 0;

    const auto synthDefDir = locateSynthDefDirectory();
    const auto catalogFile = synthDefDir.getChildFile("catalog.json");

    if (! catalogFile.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(catalogFile);

    if (! parsed.isArray())
        return;

    for (const auto& item : *parsed.getArray())
    {
        if (! item.isObject())
            continue;

        const auto* object = item.getDynamicObject();
        const auto name = object->getProperty("name").toString();

        if (name.isEmpty())
            continue;

        SynthDefDescriptor descriptor;
        descriptor.name = name;
        descriptor.type = object->getProperty("type").toString();
        descriptor.description = object->getProperty("description").toString();

        if (const auto parameters = object->getProperty("parameters"); parameters.isArray())
            for (const auto& parameter : *parameters.getArray())
                descriptor.parameters.add(parameter.toString());

        synthDefCatalog[name] = descriptor;
    }

    runtimeState.catalogedSynthDefCount = static_cast<int>(synthDefCatalog.size());
}

void SuperColliderProcessBridge::syncTransportState(const SessionState& session)
{
    const auto transportPlaying = session.transport.playing;

    syncGlobalTransportControls(session);

    if (! transportPlaying)
    {
        if (! activeRenderNodes.empty())
        {
            freeAllRenderNodes();
            runtimeState.lastOscAction = "Freed SuperCollider render nodes";
        }

        if (! activeFxNodes.empty())
        {
            freeAllFxNodes();
            runtimeState.lastOscAction = "Freed SuperCollider FX nodes";
        }

        if (! activeMidiGeneratorTracks.empty())
        {
            freeAllMidiGeneratorProxies();
            runtimeState.lastOscAction = "Freed SuperCollider MIDI mirrors";
        }

        runtimeState.transportMirroredToServer = false;
        lastTransportPlaying = false;
        return;
    }

    for (const auto& track : session.tracks)
    {
        if (track.kind != TrackKind::superColliderRender || ! track.renderScript.has_value() || ! track.renderScript->enabled)
            continue;

        if (! activeRenderNodes.contains(track.id))
            createRenderNode(track);
        else
            updateRenderNode(track);
    }

    syncFxState(session);
    syncMidiGeneratorState(session);

    runtimeState.transportMirroredToServer = true;
    runtimeState.activeRenderNodeCount = static_cast<int>(activeRenderNodes.size());
    lastTransportPlaying = transportPlaying;
}

void SuperColliderProcessBridge::createRenderNode(const TrackState& track)
{
    if (! runtimeState.oscConnected)
        return;

    const auto nodeId = synthNodeIdForTrack(track);
    activeRenderNodes[track.id] = nodeId;
    const auto synthDefName = resolveRenderSynthDefName(track);

    oscSender.send("/s_new", synthDefName, nodeId, 0, groupIdForTrack(track),
                   juce::String("freq"), frequencyForRenderTrack(track),
                   juce::String("amp"), track.mixer.volume * 0.15f);
    oscSender.send("/c_set", renderAudioBusForTrack(track), track.mixer.volume);

    runtimeState.activeRenderNodeCount = static_cast<int>(activeRenderNodes.size());
    runtimeState.lastOscAction = "Created render node for " + track.name + " using " + synthDefName;
}

void SuperColliderProcessBridge::updateRenderNode(const TrackState& track)
{
    if (! runtimeState.oscConnected)
        return;

    const auto it = activeRenderNodes.find(track.id);

    if (it == activeRenderNodes.end())
        return;

    oscSender.send("/n_set", it->second,
                   juce::String("freq"), frequencyForRenderTrack(track),
                   juce::String("amp"), (track.muted ? 0.0f : track.mixer.volume * 0.15f));
    oscSender.send("/c_set", renderAudioBusForTrack(track), (track.muted ? 0.0f : track.mixer.volume));

    runtimeState.lastOscAction = "Updated render node for " + track.name;
}

void SuperColliderProcessBridge::syncFxState(const SessionState& session)
{
    for (const auto& track : session.tracks)
    {
        const auto fxIt = std::find_if(track.inserts.begin(), track.inserts.end(), [] (const auto& insert) {
            return insert.kind == ProcessorKind::superColliderFx && insert.superCollider.has_value();
        });

        if (fxIt == track.inserts.end() || fxIt->bypassed || track.muted)
        {
            freeFxNode(track.id);
            continue;
        }

        if (! activeFxNodes.contains(track.id))
            createFxNode(track, *fxIt);
        else
            updateFxNode(track, *fxIt);
    }

    runtimeState.activeFxNodeCount = static_cast<int>(activeFxNodes.size());
}

void SuperColliderProcessBridge::createFxNode(const TrackState& track, const TrackProcessorState& insert)
{
    if (! runtimeState.oscConnected)
        return;

    const auto nodeId = fxNodeIdForTrack(track);
    activeFxNodes[track.id] = nodeId;
    const auto synthDefName = resolveFxSynthDefName(track, insert);

    oscSender.send("/s_new", synthDefName, nodeId, 1, groupIdForTrack(track),
                   juce::String("freq"), frequencyForFxTrack(track),
                   juce::String("amp"), track.mixer.volume * 0.06f);
    oscSender.send("/c_set", fxAudioBusForTrack(track), track.mixer.volume);

    runtimeState.activeFxNodeCount = static_cast<int>(activeFxNodes.size());
    runtimeState.lastOscAction = "Created FX proxy for " + track.name + " / " + insert.name + " using " + synthDefName;
}

void SuperColliderProcessBridge::updateFxNode(const TrackState& track, const TrackProcessorState& insert)
{
    if (! runtimeState.oscConnected)
        return;

    const auto it = activeFxNodes.find(track.id);

    if (it == activeFxNodes.end())
        return;

    oscSender.send("/n_set", it->second,
                   juce::String("freq"), frequencyForFxTrack(track),
                   juce::String("amp"), track.mixer.volume * 0.06f);
    oscSender.send("/c_set", fxAudioBusForTrack(track), track.mixer.volume);

    runtimeState.lastOscAction = "Updated FX proxy for " + track.name + " / " + insert.name;
}

void SuperColliderProcessBridge::freeFxNode(int trackId)
{
    const auto it = activeFxNodes.find(trackId);

    if (it == activeFxNodes.end())
        return;

    if (runtimeState.oscConnected)
        oscSender.send("/n_free", it->second);

    activeFxNodes.erase(it);
    runtimeState.activeFxNodeCount = static_cast<int>(activeFxNodes.size());
}

void SuperColliderProcessBridge::freeAllFxNodes()
{
    if (runtimeState.oscConnected)
        for (const auto& [trackId, nodeId] : activeFxNodes)
            juce::ignoreUnused(trackId), oscSender.send("/n_free", nodeId);

    activeFxNodes.clear();
    runtimeState.activeFxNodeCount = 0;
}

void SuperColliderProcessBridge::syncMidiGeneratorState(const SessionState& session)
{
    for (const auto& track : session.tracks)
    {
        const bool hasMidiGenerator = track.midiGenerator.kind == MidiGeneratorKind::superCollider
            && track.midiGenerator.superCollider.has_value()
            && track.midiGenerator.enabled;

        if (! hasMidiGenerator || track.muted)
        {
            freeMidiGeneratorProxy(track.id);
            continue;
        }

        if (! activeMidiGeneratorTracks.contains(track.id))
            createMidiGeneratorProxy(track);

        updateMidiGeneratorProxy(track, session.transport.playheadBeat);
    }

    runtimeState.activeMidiGeneratorCount = static_cast<int>(activeMidiGeneratorTracks.size());
}

void SuperColliderProcessBridge::createMidiGeneratorProxy(const TrackState& track)
{
    if (! runtimeState.oscConnected)
        return;

    activeMidiGeneratorTracks[track.id] = true;
    oscSender.send("/c_set", midiControlBusForTrack(track), 0.0f);
    oscSender.send("/c_set", midiControlBusForTrack(track) + 1, static_cast<float>(track.mixer.volume));
    runtimeState.activeMidiGeneratorCount = static_cast<int>(activeMidiGeneratorTracks.size());
    runtimeState.lastOscAction = "Created MIDI control mirror for " + track.name;
}

void SuperColliderProcessBridge::updateMidiGeneratorProxy(const TrackState& track, double phaseBeat)
{
    if (! runtimeState.oscConnected)
        return;

    const auto pulse = static_cast<float>(0.5 + 0.5 * std::sin(phaseBeat * juce::MathConstants<double>::halfPi * (1.0 + track.id * 0.25)));
    oscSender.send("/c_set", midiControlBusForTrack(track), pulse);
    oscSender.send("/c_set", midiControlBusForTrack(track) + 1, track.mixer.volume);
    runtimeState.lastOscAction = "Updated MIDI mirror for " + track.name;
}

void SuperColliderProcessBridge::freeMidiGeneratorProxy(int trackId)
{
    const auto it = activeMidiGeneratorTracks.find(trackId);

    if (it == activeMidiGeneratorTracks.end())
        return;

    if (runtimeState.oscConnected)
    {
        oscSender.send("/c_set", runtimeState.rootGroupId + 400 + trackId * 2, 0.0f);
        oscSender.send("/c_set", runtimeState.rootGroupId + 400 + trackId * 2 + 1, 0.0f);
    }

    activeMidiGeneratorTracks.erase(it);
    runtimeState.activeMidiGeneratorCount = static_cast<int>(activeMidiGeneratorTracks.size());
}

void SuperColliderProcessBridge::freeAllMidiGeneratorProxies()
{
    if (runtimeState.oscConnected)
        for (const auto& [trackId, active] : activeMidiGeneratorTracks)
        {
            juce::ignoreUnused(active);
            oscSender.send("/c_set", runtimeState.rootGroupId + 400 + trackId * 2, 0.0f);
            oscSender.send("/c_set", runtimeState.rootGroupId + 400 + trackId * 2 + 1, 0.0f);
        }

    activeMidiGeneratorTracks.clear();
    runtimeState.activeMidiGeneratorCount = 0;
}

void SuperColliderProcessBridge::freeRenderNode(int trackId)
{
    const auto it = activeRenderNodes.find(trackId);

    if (it == activeRenderNodes.end())
        return;

    if (runtimeState.oscConnected)
        oscSender.send("/n_free", it->second);

    activeRenderNodes.erase(it);
    runtimeState.activeRenderNodeCount = static_cast<int>(activeRenderNodes.size());
}

void SuperColliderProcessBridge::freeAllRenderNodes()
{
    if (runtimeState.oscConnected)
        for (const auto& [trackId, nodeId] : activeRenderNodes)
            juce::ignoreUnused(trackId), oscSender.send("/n_free", nodeId);

    activeRenderNodes.clear();
    runtimeState.activeRenderNodeCount = 0;
}

int SuperColliderProcessBridge::synthNodeIdForTrack(const TrackState& track) const
{
    return runtimeState.rootGroupId + 100 + track.id;
}

int SuperColliderProcessBridge::fxNodeIdForTrack(const TrackState& track) const
{
    return runtimeState.rootGroupId + 200 + track.id;
}

int SuperColliderProcessBridge::midiControlBusForTrack(const TrackState& track) const
{
    if (const auto it = midiControlBuses.find(track.id); it != midiControlBuses.end())
        return it->second;

    return runtimeState.rootGroupId + 400 + track.id * 2;
}

int SuperColliderProcessBridge::renderAudioBusForTrack(const TrackState& track) const
{
    if (const auto it = renderAudioBuses.find(track.id); it != renderAudioBuses.end())
        return it->second;

    return 64 + track.id * 2;
}

int SuperColliderProcessBridge::fxAudioBusForTrack(const TrackState& track) const
{
    if (const auto it = fxAudioBuses.find(track.id); it != fxAudioBuses.end())
        return it->second;

    return 128 + track.id * 2;
}

juce::File SuperColliderProcessBridge::locateSynthDefDirectory() const
{
    const auto cwdCandidate = juce::File::getCurrentWorkingDirectory().getChildFile("SynthDefs");

    if (cwdCandidate.isDirectory())
        return cwdCandidate;

    auto executableParent = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    for (int i = 0; i < 6; ++i)
    {
        const auto candidate = executableParent.getChildFile("SynthDefs");

        if (candidate.isDirectory())
            return candidate;

        executableParent = executableParent.getParentDirectory();
    }

    return cwdCandidate;
}

juce::String SuperColliderProcessBridge::escapeForScString(const juce::String& text) const
{
    auto escaped = text.replace("\\", "\\\\")
                       .replace("\"", "\\\"");
    escaped = escaped.replace("\r\n", "\\n")
                     .replace("\n", "\\n")
                     .replace("\r", "\\n");
    return escaped;
}

bool SuperColliderProcessBridge::runSclangScript(const juce::String& scriptBody, int timeoutMs, juce::String& output) const
{
    output.clear();

    if (! runtimeState.sclangDetected)
        return false;

    auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("cigol-sc-run", ".scd", false);

    if (! tempFile.replaceWithText(scriptBody))
        return false;

    juce::ChildProcess process;
    const auto started = process.start(juce::StringArray { runtimeState.sclangPath, tempFile.getFullPathName() });

    if (! started)
    {
        tempFile.deleteFile();
        return false;
    }

    process.waitForProcessToFinish(timeoutMs);
    output = process.readAllProcessOutput();
    tempFile.deleteFile();
    return process.getExitCode() == 0;
}

juce::String SuperColliderProcessBridge::resolveRenderSynthDefName(const TrackState& track) const
{
    if (track.renderScript.has_value() && track.renderScript->synthDefName.isNotEmpty() && isSynthDefAvailable(track.renderScript->synthDefName))
        return track.renderScript->synthDefName;

    return "default";
}

juce::String SuperColliderProcessBridge::resolveFxSynthDefName(const TrackState& track, const TrackProcessorState& insert) const
{
    juce::ignoreUnused(track);

    if (insert.superCollider.has_value() && insert.superCollider->synthDefName.isNotEmpty() && isSynthDefAvailable(insert.superCollider->synthDefName))
        return insert.superCollider->synthDefName;

    return "default";
}

bool SuperColliderProcessBridge::isSynthDefAvailable(const juce::String& synthDefName) const
{
    return availableSynthDefs.contains(synthDefName);
}

const SynthDefDescriptor* SuperColliderProcessBridge::findSynthDefDescriptor(const juce::String& synthDefName) const
{
    if (const auto it = synthDefCatalog.find(synthDefName); it != synthDefCatalog.end())
        return &it->second;

    return nullptr;
}

float SuperColliderProcessBridge::frequencyForRenderTrack(const TrackState& track) const
{
    return 220.0f + static_cast<float>(track.id * 55);
}

float SuperColliderProcessBridge::frequencyForFxTrack(const TrackState& track) const
{
    return 90.0f + static_cast<float>(track.id * 18);
}

int SuperColliderProcessBridge::groupIdForTrack(const TrackState& track) const
{
    return runtimeState.rootGroupId + 1 + track.id;
}

void SuperColliderProcessBridge::allocateTrackBuses(const SessionState& session)
{
    renderAudioBuses.clear();
    fxAudioBuses.clear();
    midiControlBuses.clear();

    int nextAudioBus = 64;
    int nextFxBus = 128;
    int nextControlBus = 256;

    for (const auto& track : session.tracks)
    {
        if (track.kind == TrackKind::superColliderRender)
        {
            renderAudioBuses[track.id] = nextAudioBus;
            nextAudioBus += 2;
        }

        if (std::any_of(track.inserts.begin(), track.inserts.end(), [] (const auto& insert) {
                return insert.kind == ProcessorKind::superColliderFx && insert.superCollider.has_value();
            }))
        {
            fxAudioBuses[track.id] = nextFxBus;
            nextFxBus += 2;
        }

        if (track.midiGenerator.kind == MidiGeneratorKind::superCollider && track.midiGenerator.superCollider.has_value())
        {
            midiControlBuses[track.id] = nextControlBus;
            nextControlBus += 2;
        }
    }

    runtimeState.allocatedAudioBusCount = static_cast<int>(renderAudioBuses.size() * 2 + fxAudioBuses.size() * 2);
    runtimeState.allocatedControlBusCount = static_cast<int>(midiControlBuses.size() * 2 + 3);
}

void SuperColliderProcessBridge::syncGlobalTransportControls(const SessionState& session)
{
    if (! runtimeState.oscConnected)
        return;

    oscSender.send("/c_set", runtimeState.transportControlBus, session.transport.playing ? 1.0f : 0.0f);
    oscSender.send("/c_set", runtimeState.tempoControlBus, static_cast<float>(session.transport.bpm));
    oscSender.send("/c_set", runtimeState.playheadControlBus, static_cast<float>(session.transport.playheadBeat));
}
} // namespace cigol

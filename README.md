# LogicLikeDAW

A JUCE desktop DAW foundation aimed at a Logic Pro style workflow, with SuperCollider treated as a first-class engine feature rather than an afterthought.

Current app structure:

- transport, timeline, arrange view, inspector, mixer, and SuperCollider routing overview
- track models for audio, MIDI, instrument, and dedicated SuperCollider render tracks
- insert models for Audio Units and SuperCollider-powered audio processors
- MIDI generator models that can be driven by SuperCollider pattern code
- audio device management and a preview engine that keeps transport and meters alive

## SuperCollider Model

This project now explicitly supports three SuperCollider-oriented concepts in the session model:

1. `SuperCollider render tracks`
   These are tracks whose source is SuperCollider code and which are intended to render or preview generated audio.
2. `SuperCollider FX inserts`
   These sit on audio tracks like insert plugins and conceptually process incoming audio through SuperCollider code.
3. `SuperCollider MIDI generators`
   These feed MIDI tracks from SuperCollider patterns or event generators.

The current bridge in `Source/Engine/SuperColliderBridge.*` now does real environment discovery and process management:

- detects `SuperCollider.app`
- locates `scsynth` and `sclang`
- boots a local `scsynth` server process for the DAW session
- connects an OSC sender to the running `scsynth` instance
- sends session bootstrap commands like `/notify`, `/clearSched`, `/g_new`, and `/status`
- creates and updates real `scsynth` synth nodes for SuperCollider render tracks while transport is running
- creates live proxy nodes for SuperCollider FX inserts while transport is running
- mirrors SuperCollider MIDI generator state onto live control buses in `scsynth`
- allocates stable audio and control buses for render, FX, MIDI, tempo, playhead, and transport state
- scans a local `SynthDefs` directory and sends `/d_loadDir` so compiled `.scsyndef` assets can be used at runtime
- can auto-compile synthdef source entries from `SynthDefs/manifest.tsv` when `sclang` is launchable from the app runtime
- reads a synthdef catalog so named render/FX definitions have repo-level metadata and parameter contracts
- includes a repo build script for synthdef validation/reporting/compilation
- includes an in-app `Rebuild SynthDefs` action in the SuperCollider routing panel for manual refresh/reload
- reflects live runtime state back into the DAW routing model

It does not yet evaluate arbitrary SuperCollider code. Instead, it can now load precompiled `.scsyndef` assets from `SynthDefs/`, attempt app-driven compilation from the repo source templates when possible, resolve track/insert `synthDefName` values against those assets, and read synthdef metadata from `SynthDefs/catalog.json`, with fallback to SuperCollider's built-in `default` synth when needed. SuperCollider FX and MIDI generator paths are still proxy/control implementations rather than full DSP or pattern execution.

## SuperColliderAU Fit

The repository you pointed to, `supercollider/SuperColliderAU`, is a useful fit for the FX and instrument-style hosting side because it wraps a SuperCollider server inside an Audio Unit on macOS and can be controlled externally. That makes it a strong candidate for:

- hosting SuperCollider-based audio processors in the insert chain
- hosting SuperCollider-based instruments on instrument tracks
- reusing a plugin-hosting path instead of only building a custom OSC bridge

For dedicated render tracks and MIDI generators, a direct SuperCollider process bridge is still likely useful alongside any Audio Unit hosting path.

## Source Layout

- `Source/Core`
  Session and routing models
- `Source/Engine`
  Audio engine and SuperCollider bridge layer
- `Source/UI`
  Main editor, arrange view, inspector, mixer, and app look-and-feel
- `Source/App`
  JUCE application and main window bootstrap

## Build

This project expects JUCE to be installed and discoverable by CMake.

Example:

```bash
cmake -S . -B build
cmake --build build
```

This repo will automatically use `/Users/user/Documents/Fabric/JUCE` when present. Otherwise, set `JUCE_DIR` or `LOGICLIKEDAW_JUCE_PATH` to a local JUCE checkout.

## What Is Real Now

- the DAW is split into maintainable app, core, engine, and UI modules
- the session model already carries SuperCollider render, FX, and MIDI generator state
- the UI exposes those concepts in the track list, inspector, mixer, and routing overview
- the app can now detect the installed SuperCollider bundle and boot `scsynth`
- the app now sends live OSC session-management traffic to `scsynth`
- SuperCollider render tracks now create live synth nodes in `scsynth` when transport runs
- SuperCollider FX inserts now create live proxy nodes in `scsynth`
- SuperCollider MIDI generators now mirror transport-synced control values into `scsynth`
- the bridge now allocates stable SC audio/control buses for track and transport routing
- the bridge now loads compiled synthdefs from [SynthDefs/README.md](/Users/user/Documents/New%20project%203/SynthDefs/README.md) when `.scsyndef` files are present
- the bridge now reads synthdef metadata from [catalog.json](/Users/user/Documents/New%20project%203/SynthDefs/catalog.json)
- the repo now includes [scripts/build_synthdefs.sh](/Users/user/Documents/New%20project%203/scripts/build_synthdefs.sh) plus [manifest.tsv](/Users/user/Documents/New%20project%203/SynthDefs/manifest.tsv) for synthdef build/report flow
- the audio engine now runs through a JUCE `AudioProcessorGraph`
- AU hosting is scaffolded through JUCE’s plugin format manager

## Current SuperCollider Runtime Note

On this machine, `scsynth` is available and can be started by the bridge. The app now probes `sclang` by running a real temporary `.scd` script, matching the ModDAW approach, and will try to auto-compile synthdefs from `SynthDefs/manifest.tsv` when that path succeeds. In this terminal session, direct `sclang` launches still report a Qt/processor compatibility error, so the remaining gap is specifically about launch/runtime context, not synthdef workflow structure. That means:

- SuperCollider render-track and FX server plumbing can move forward
- SuperCollider MIDI generation and arbitrary code-evaluation still need either a consistently working `sclang` runtime path or an alternate evaluation path

## Next Serious Steps

1. compile the starter `.scd` files into real `.scsyndef` assets so the runtime stops falling back to `default`
2. resolve `sclang` execution on this machine or introduce an alternate code-evaluation path
3. instantiate real AU plugins inside the graph and evaluate `SuperColliderAU` as an insert/instrument host path
4. replace placeholder graph tone generators with clip playback, instruments, and routed processing nodes
5. add offline bouncing for SuperCollider render tracks and proper return audio capture
6. add MIDI editing, live input, SuperCollider pattern capture, and project persistence

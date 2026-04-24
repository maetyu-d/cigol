// Loaded by scripts/build_synthdefs.sh on a machine with working sclang.
~logicLikeSynthDef = SynthDef(\nebulaScene, { |out = 0, freq = 220, amp = 0.15, gate = 1|
    var signal = Mix(SinOsc.ar([freq, freq * 1.5, freq * 2.0], 0, amp * [0.8, 0.5, 0.25]));
    signal = GVerb.ar(signal, 60, 5, 0.4, 0.7, 15, 1, 0.6, 0.5, 200);
    signal = signal * EnvGen.kr(Env.asr(0.02, 1.0, 1.2), gate, doneAction: 2);
    Out.ar(out, signal.dup);
});

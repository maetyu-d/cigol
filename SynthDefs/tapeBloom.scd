// Loaded by scripts/build_synthdefs.sh on a machine with working sclang.
~logicLikeSynthDef = SynthDef(\tapeBloom, { |inBus = 0, out = 0, drive = 1.4, mix = 0.7|
    var dry = In.ar(inBus, 2);
    var wet = tanh(dry * drive);
    wet = LPF.ar(wet, 5500);
    Out.ar(out, XFade2.ar(dry, wet, (mix * 2.0) - 1.0));
});
